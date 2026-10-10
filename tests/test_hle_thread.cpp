#include <atomic>
#include <cassert>
#include <chrono>
#include <latch>
#include <thread>
#include "core/libraries/kernel/threads.h"

extern "C" void* runtime_thread_tcb(void);

int main() {
    using Libraries::Kernel::Thread;
    for (int iteration = 0; iteration < 100; ++iteration) {
        Thread worker;
        std::latch started{1}, joining{2};
        std::atomic<bool> done{};
        worker.Run([&](std::stop_token stop) {
            assert(runtime_thread_tcb());
            started.count_down();
            joining.wait();
            while (!stop.stop_requested()) std::this_thread::yield();
            worker.Join(); // self completion races external Join + Stop
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            done = true;
        });
        started.wait();
        std::thread a([&] { joining.count_down(); worker.Join(); assert(done); });
        std::thread b([&] { joining.count_down(); worker.Stop(); assert(done); });
        a.join(); b.join();
        assert(!worker.Joinable());
        worker.Stop(); // repeated stop must be harmless
        std::atomic<bool> restarted{};
        worker.Run([&](std::stop_token) { worker.Join(); restarted = true; });
        worker.Join();
        assert(restarted && !worker.Joinable());
    }
}
