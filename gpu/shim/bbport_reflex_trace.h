#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace BbReflexTrace {
inline std::atomic<unsigned long long> epoch{0};
inline void Event(const char* stage, uintptr_t caller = 0) {
    static const bool enabled = [] {
        const char* value = std::getenv("BB_REFLEX_TRACE");
        return value && !std::strcmp(value, "1");
    }();
    if (!enabled)
        return;
    const auto frame = epoch.load();
    thread_local unsigned long long last_input = ~0ull;
    if (!std::strcmp(stage, "input")) {
        if (last_input == frame)
            return;
        last_input = frame;
    }
    static std::mutex output;
    const auto clock = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    std::scoped_lock lock(output);
    std::printf("Reflex trace: stage=%s epoch=%llu thread=%llu clock=%lld caller=%llx\n", stage,
                frame, (unsigned long long)std::hash<std::thread::id>{}(std::this_thread::get_id()),
                (long long)clock, (unsigned long long)caller);
}
inline void End() {
    Event("submit_done");
    epoch.fetch_add(1);
}
} // namespace BbReflexTrace
