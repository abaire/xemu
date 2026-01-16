#ifndef QEMU_THREAD_WIN32_H
#define QEMU_THREAD_WIN32_H

#include <windows.h>

typedef struct QemuThreadData QemuThreadData;
struct QemuThread {
    QemuThreadData *data;
    unsigned tid;
};

struct QemuMutex {
    SRWLOCK lock;
#ifdef CONFIG_DEBUG_MUTEX
    const char *file;
    int line;
    QemuThread owner;
#endif
    bool initialized;
};

typedef struct QemuRecMutex QemuRecMutex;
struct QemuRecMutex {
    CRITICAL_SECTION lock;
    bool initialized;
};

struct QemuCond {
    CONDITION_VARIABLE var;
    bool initialized;
};

struct QemuSemaphore {
    HANDLE sema;
    bool initialized;
};

struct QemuEvent {
    int value;
    HANDLE event;
    bool initialized;
};

/* Only valid for joinable threads.  */
HANDLE qemu_thread_get_handle(struct QemuThread *thread);

#endif
