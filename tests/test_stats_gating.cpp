// Disabled diagnostics must leave operation and wait counters untouched,
// including simultaneous scheduler-style waits from multiple threads.
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>
#include "bbport_toggles.h"

int main(int argc, char** argv) {
    assert(argc == 2);
    const bool expected = std::strcmp(argv[1], "enabled") == 0;
    assert(BbStats::enabled == expected);
    std::atomic<std::uint64_t> operations{13}, waits{17};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 4; ++i) {
        workers.emplace_back([&] {
            for (unsigned n = 0; n < 20; ++n) {
                BbStats::Timer operation{operations};
                BbStats::WaitTimer wait{waits};
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        });
    }
    for (auto& worker : workers) worker.join();
    if (expected) {
        assert(operations.load() > 13 && waits.load() > 17);
    } else {
        assert(operations.load() == 13 && waits.load() == 17);
    }
}
