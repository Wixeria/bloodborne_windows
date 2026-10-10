// Windows render-clock waits: deadlines, APC interruption and timer lifetime.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <latch>
#include <thread>
#include <vector>
#include <windows.h>
#include "common/thread.h"

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

static void short_waits() {
    for (unsigned i = 0; i < 120; ++i) {
        const auto start = Clock::now();
        std::chrono::nanoseconds remaining = 1s;
        assert(Common::AccurateSleep(50us, &remaining, false));
        assert(Clock::now() - start >= 50us);
        assert(remaining == 0ns);
    }
}

int main() {
    std::chrono::nanoseconds remaining = 1s;
    assert(Common::AccurateSleep(0ns, &remaining, false) && remaining == 0ns);
    assert(Common::AccurateSleep(-1ns, nullptr, false));
    Common::SleepUntil(Clock::now() - 1ms);
    const auto deadline = Clock::now() + 2ms;
    Common::SleepUntil(deadline);
    assert(Clock::now() >= deadline);

    // Warm the calling thread's timer and the native thread library before counts.
    short_waits();
    std::thread(short_waits).join();
    DWORD before = 0, after = 0;
    assert(GetProcessHandleCount(GetCurrentProcess(), &before));
    short_waits();
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 8; ++i) workers.emplace_back(short_waits);
    for (auto& worker : workers) worker.join();
    assert(GetProcessHandleCount(GetCurrentProcess(), &after));
    assert(after <= before + 2); // No per-wait or per-terminated-thread timer leak.

    std::latch ready(1);
    DWORD thread_id = 0;
    std::atomic<bool> called{false};
    std::thread interrupted([&] {
        thread_id = GetCurrentThreadId();
        ready.count_down();
        std::chrono::nanoseconds left = 0ns;
        assert(!Common::AccurateSleep(1s, &left, true));
        assert(called.load() && left > 0ns && left <= 1s);
        // The canceled wait must not affect the thread's next use of its timer.
        const auto start = Clock::now();
        assert(Common::AccurateSleep(2ms, nullptr, false));
        assert(Clock::now() - start >= 2ms);
    });
    ready.wait();
    HANDLE thread = OpenThread(THREAD_SET_CONTEXT, FALSE, thread_id);
    assert(thread);
    assert(QueueUserAPC([](ULONG_PTR data) {
        reinterpret_cast<std::atomic<bool>*>(data)->store(true);
    }, thread, reinterpret_cast<ULONG_PTR>(&called)));
    assert(CloseHandle(thread));
    interrupted.join();

    // Diagnostic overshoot, not a fragile upper-bound assertion on host scheduling.
    std::vector<double> overshoot;
    for (unsigned i = 0; i < 120; ++i) {
        const auto end = Clock::now() + 2083333ns;
        Common::SleepUntil(end);
        overshoot.push_back(std::chrono::duration<double, std::micro>(Clock::now() - end).count());
    }
    std::sort(overshoot.begin(), overshoot.end());
    std::printf("Render wait: median overshoot %.1f us, p99 %.1f us; handle delta %ld; APC PASS\n",
        overshoot[60], overshoot[118], static_cast<long>(after) - static_cast<long>(before));
}
