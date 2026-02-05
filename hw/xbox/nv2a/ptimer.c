/*
 * QEMU Geforce NV2A implementation
 * PTIMER - time measurement and time-based alarms
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "nv2a_int.h"

#define CLOCK_HIGH_MASK 0x1fffffff
#define CLOCK_LOW_MASK 0xffffffe0
#define ALARM_MASK 0xffffffe0

static void ptimer_alarm_fired(void *opaque);

void ptimer_reset(NV2AState *d)
{
    d->ptimer.alarm_time = 0;
    d->ptimer.alarm_time_high = 0;
    d->ptimer.time_offset = 0;
    timer_del(&d->ptimer.timer);
}

void ptimer_init(NV2AState *d)
{
    timer_init_ns(&d->ptimer.timer, QEMU_CLOCK_VIRTUAL, ptimer_alarm_fired, d);
    ptimer_reset(d);
}

static uint64_t ptimer_get_absolute_clock(NV2AState *d)
{
    return muldiv64(muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                             d->pramdac.core_clock_freq,
                             NANOSECONDS_PER_SECOND),
                    d->ptimer.denominator, d->ptimer.numerator);
}

//! Returns the NV2A time adjusted by the last time register modification and
//! shifted into register range.
static uint64_t get_ptimer_clock(NV2AState *d, uint64_t absolute_clock)
{
    return ((absolute_clock + d->ptimer.time_offset) << 5) &
           0x1fffffffffffffffULL;
}

static uint64_t ptimer_ticks_to_ns(NV2AState *d, uint64_t ticks)
{
    uint64_t gpu_ticks =
        muldiv64(ticks, d->ptimer.numerator, d->ptimer.denominator);
    return muldiv64(gpu_ticks, NANOSECONDS_PER_SECOND,
                    d->pramdac.core_clock_freq);
}

static uint64_t last_schedule_now = 0;
static uint64_t last_schedule_alarm = 0;
static uint64_t last_schedule_diff_ns = 0;

static void schedule_qemu_timer(NV2AState *d)
{
    if (!(d->ptimer.enabled_interrupts & NV_PTIMER_INTR_EN_0_ALARM)) {
        return;
    }

    uint64_t now = get_ptimer_clock(d, ptimer_get_absolute_clock(d));
    uint64_t alarm_time =
        ((uint64_t)d->ptimer.alarm_time_high << 32) + d->ptimer.alarm_time;

    uint64_t diff_ns = 0;
    if (alarm_time > now) {
        diff_ns = ptimer_ticks_to_ns(d, (alarm_time - now) >> 5);
        last_schedule_now = now;
        last_schedule_alarm = alarm_time;
        last_schedule_diff_ns = diff_ns;
        fprintf(stderr, "Schedule ptimer alarm %llu - now %llu = %llu ns\n",
                alarm_time, now, diff_ns);
    } else {
        fprintf(
            stderr,
            "\n\n!!! PTIMER QEMU TIMER SCHEDULED FOR TIMER IN THE PAST: alarm "
            "%llu - now %llu = %llu ns\n",
            alarm_time, now, diff_ns);
    }
    timer_mod(&d->ptimer.timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + diff_ns);
}

static uint64_t last_alarm_fire = 0;
static void ptimer_alarm_fired(void *opaque)
{
    NV2AState *d = (NV2AState *)opaque;

    if (!(d->ptimer.enabled_interrupts & NV_PTIMER_INTR_EN_0_ALARM)) {
        return;
    }

    uint64_t now = get_ptimer_clock(d, ptimer_get_absolute_clock(d));
    if (last_alarm_fire) {
        uint64_t delta_time = now - last_alarm_fire;
        fprintf(stderr,
                "PTIMER alarm fired after %llu ns. Was scheduled for %llu ns\n",
                delta_time, last_schedule_diff_ns);
        if (delta_time > last_alarm_fire) {
            fprintf(stderr, "  Late by %llu ns\n",
                    delta_time - last_schedule_diff_ns);
        }
    } else {
        fprintf(stderr, "PTIMER alarm fired!\n");
    }
    last_alarm_fire = now;

    uint64_t alarm_time =
        d->ptimer.alarm_time + ((uint64_t)d->ptimer.alarm_time_high << 32);

    if (alarm_time <= now) {
        d->ptimer.pending_interrupts |= NV_PTIMER_INTR_0_ALARM;
        d->ptimer.alarm_time_high = (now >> 32) + 1;
        nv2a_update_irq(d);
    } else {
        fprintf(stderr, "PTIMER alarm fired early, rescheduling\n");
    }

    schedule_qemu_timer(d);
}

uint64_t ptimer_read(void *opaque, hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PTIMER_INTR_0:
        r = d->ptimer.pending_interrupts;
        break;
    case NV_PTIMER_INTR_EN_0:
        r = d->ptimer.enabled_interrupts;
        break;
    case NV_PTIMER_NUMERATOR:
        r = d->ptimer.numerator;
        break;
    case NV_PTIMER_DENOMINATOR:
        r = d->ptimer.denominator;
        break;
    case NV_PTIMER_TIME_0: {
        uint64_t now = get_ptimer_clock(d, ptimer_get_absolute_clock(d));
        r = now & 0xffffffff;

        fprintf(stderr, "READ PTIMER TIME_0: 0x%X = 0x%X\n", addr, r);
    } break;
    case NV_PTIMER_TIME_1: {
        uint64_t now = get_ptimer_clock(d, ptimer_get_absolute_clock(d));
        r = (now >> 32) & CLOCK_HIGH_MASK;

        fprintf(stderr, "READ PTIMER TIME_1: 0x%X = 0x%X\n", addr, r);
    } break;
    case NV_PTIMER_ALARM_0:
        r = d->ptimer.alarm_time;
        fprintf(stderr, "READ PTIMER ALARM0: 0x%X = 0x%X\n", addr, r);
        break;
    default:
        break;
    }

    nv2a_reg_log_read(NV_PTIMER, addr, size, r);
    return r;
}

void ptimer_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    nv2a_reg_log_write(NV_PTIMER, addr, size, val);

    switch (addr) {
    case NV_PTIMER_INTR_0:
        fprintf(stderr, "\nPTIMER 0x%X (INTR_0) = 0x%X\n", addr, val);
        d->ptimer.pending_interrupts &= ~val;
        nv2a_update_irq(d);
        break;
    case NV_PTIMER_INTR_EN_0:
        fprintf(stderr, "\nPTIMER 0x%X (INTR_EN_0) = 0x%X\n", addr, val);
        d->ptimer.enabled_interrupts = val;
        if (val) {
            schedule_qemu_timer(d);
        } else {
            timer_del(&d->ptimer.timer);
        }
        nv2a_update_irq(d);
        break;
    case NV_PTIMER_DENOMINATOR:
        fprintf(stderr, "\nPTIMER 0x%X (DENOMINATOR) = 0x%X\n", addr, val);
        d->ptimer.denominator = val;
        schedule_qemu_timer(d);
        break;
    case NV_PTIMER_NUMERATOR:
        fprintf(stderr, "\nPTIMER 0x%X (NUMERATOR) = 0x%X\n", addr, val);
        d->ptimer.numerator = val;
        schedule_qemu_timer(d);
        break;
    case NV_PTIMER_ALARM_0: {
        fprintf(stderr, "\nPTIMER 0x%X (ALARM_0) = 0x%X\n", addr, val);
        uint64_t now = get_ptimer_clock(d, ptimer_get_absolute_clock(d));
        uint64_t now_time_low = now & ALARM_MASK;

        d->ptimer.alarm_time = val & ALARM_MASK;
        d->ptimer.alarm_time_high = (now >> 32);
        if (val <= now_time_low) {
            ++d->ptimer.alarm_time_high;

            fprintf(stderr,
                    "?? PTIMER alarm scheduled in the past: %lu < %lu\n", val,
                    now_time_low);

            d->ptimer.pending_interrupts |= NV_PTIMER_INTR_0_ALARM;
            nv2a_update_irq(d);
        }

        schedule_qemu_timer(d);
    } break;
    case NV_PTIMER_TIME_0: {
        fprintf(stderr, "\nPTIMER 0x%X (TIME_0) = 0x%X\n", addr, val);
        uint64_t absolute_clock = ptimer_get_absolute_clock(d);
        uint64_t now = get_ptimer_clock(d, absolute_clock);
        uint64_t target_relative =
            (now & 0xffffffff00000000ULL) | (val & CLOCK_LOW_MASK);
        d->ptimer.time_offset = target_relative - absolute_clock;
        schedule_qemu_timer(d);
    } break;
    case NV_PTIMER_TIME_1: {
        fprintf(stderr, "\nPTIMER 0x%X (TIME_1) = 0x%X\n", addr, val);
        uint64_t absolute_clock = ptimer_get_absolute_clock(d);
        uint64_t now = get_ptimer_clock(d, absolute_clock);
        uint64_t target_relative =
            (now & 0xffffffff) | ((val & CLOCK_HIGH_MASK) << 32);
        d->ptimer.time_offset = target_relative - absolute_clock;

        now = get_ptimer_clock(d, absolute_clock);
        d->ptimer.alarm_time_high = (now >> 32);
        if (d->ptimer.alarm_time <= (now & ALARM_MASK)) {
            ++d->ptimer.alarm_time_high;
        }
        schedule_qemu_timer(d);
    } break;
    default:
        fprintf(stderr, "\nPTIMER 0x%X (???) = 0x%X\n", addr, val);
        break;
    }
}
