// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: helper thread sizing. Counts follow the hardware threads this process may run on
// (the affinity mask, so `taskset` can emulate a Steam Deck), and speculative helpers run as
// SCHED_IDLE: they use cores the game leaves idle and never take time from its threads.

#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace BbThreads {

/// Hardware threads available to the process.
inline unsigned Available() {
#ifdef _WIN32
    DWORD_PTR process,system;
    if (GetProcessAffinityMask(GetCurrentProcess(),&process,&system))
        return std::max(1u,static_cast<unsigned>(__builtin_popcountll(process)));
#else
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        return std::max(1, CPU_COUNT(&set));
    }
#endif
    return std::max(1u, std::thread::hardware_concurrency());
}

/// The calling thread only runs on otherwise idle cores (falls back to the lowest nice level).
inline void MakeBackground() {
#ifdef _WIN32
    // Match the offline port: CPU-idle helpers, with normal I/O and memory
    // priority. Windows background mode also lowers I/O/memory priority, unlike
    // Linux SCHED_IDLE. Keep that former mode available for an isolated A/B run.
    const char* mode = std::getenv("BB_HELPER_PRIORITY");
    if (mode && std::strcmp(mode,"background") == 0) {
        if (!SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN))
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_LOWEST);
    } else {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_IDLE);
    }
#else
    sched_param param{};
    if (sched_setscheduler(0, SCHED_IDLE, &param) != 0) {
        setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), 19);
    }
#endif
}

} // namespace BbThreads
