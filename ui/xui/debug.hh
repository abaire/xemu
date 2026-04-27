//
// xemu User Interface
//
// Copyright (C) 2020-2022 Matt Borgerson
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#pragma once

#include <vector>
#include <string_view>
#include <array>

#include "hw/xbox/nv2a/debug.h"

class DebugApuWindow
{
public:
    bool m_is_open;
    DebugApuWindow();
    void Draw();
};

namespace nv2a_debug {
    struct AccumulatorNode {
        int parent_index;
        std::array<int, NV2A_PROF_ACCUMULATORS__COUNT> children;
        int num_children;
    };

    constexpr std::array<AccumulatorNode, NV2A_PROF_ACCUMULATORS__COUNT> BuildAccumulatorTree() {
        std::array<AccumulatorNode, NV2A_PROF_ACCUMULATORS__COUNT> tree{};
        for (int i = 0; i < NV2A_PROF_ACCUMULATORS__COUNT; ++i) {
            tree[i].parent_index = -1;
            tree[i].num_children = 0;
        }

        constexpr std::array<std::string_view, NV2A_PROF_ACCUMULATORS__COUNT> names = {
            #define _X(x) std::string_view(#x).substr(10),
            NV2A_PROF_ACCUMULATORS_XMAC
            #undef _X
        };

        for (int i = 0; i < NV2A_PROF_ACCUMULATORS__COUNT; ++i) {
            std::string_view name = names[i];
            auto pos = name.rfind("__");
            if (pos != std::string_view::npos) {
                std::string_view parent_name = name.substr(0, pos);
                for (int j = 0; j < NV2A_PROF_ACCUMULATORS__COUNT; ++j) {
                    if (names[j] == parent_name) {
                        tree[i].parent_index = j;
                        tree[j].children[tree[j].num_children++] = i;
                        break;
                    }
                }
            }
        }
        return tree;
    }

    inline constexpr auto kAccumulatorTree = BuildAccumulatorTree();
}

class DebugVideoWindow
{
public:
    enum class LegendSortMode : int {
        DEFAULT,
        ALPHABETICAL_AZ,
        ALPHABETICAL_ZA,
        VALUE
    } m_legend_sort_mode;

    bool m_is_open;
    bool m_transparent;
    bool m_position_restored;
    bool m_resize_init_complete;
    float m_prev_scale;

    DebugVideoWindow();
    void Draw();

private:
    struct CounterEntry {
        int index;
        int value;
    };

    static constexpr int NUM_EXTRA_COUNTERS = 1;
    static constexpr int INDEX_MSPF = NV2A_PROF__COUNT;
    static constexpr int INDEX_TIMING_MSPF = NV2A_PROF_ACCUMULATORS__COUNT;

    int m_hovered_counter_index;
    int m_hovered_accumulator_index;
    bool m_counter_visible[NV2A_PROF__COUNT + NUM_EXTRA_COUNTERS];
    bool m_timing_visible[NV2A_PROF_ACCUMULATORS__COUNT + 1];

    std::vector<const char *> m_legend_names;
    std::vector<int> m_legend_indices_sorted_az;

    std::vector<CounterEntry> m_counter_index_to_value;

    void DrawAdvancedContent();
    void DrawFrameTimingBreakdownContent();
    int FindHoveredPlotLineIndex();
    int FindHoveredAccumulatorIndex();

    double GetVisibleDescendantsTotal(int acc_index, int frame_idx) const;
    double GetAccumulatorOwnValue(int acc_index, int frame_idx) const;
    bool IsDescendantOrSelf(int ancestor, int node) const;
};

extern DebugApuWindow apu_window;
extern DebugVideoWindow video_window;
