// Media transitions may clear a frame queue while playback consumes it.
#include <array>
#include <atomic>
#include <cassert>
#include <memory>
#include <thread>
#include <vector>
#include "core/libraries/avplayer/avplayer_common.h"

int main() {
    using Libraries::AvPlayer::AvPlayerQueue;
    AvPlayerQueue<int> timed;
    assert(!timed.Pop());
    timed.Push(100);
    assert(!timed.PopIf([](int timestamp) { return timestamp <= 99; }));
    assert(timed.Size() == 1);
    auto ready = timed.PopIf([](int timestamp) { return timestamp <= 100; });
    assert(ready && *ready == 100 && timed.Size() == 0);

    constexpr unsigned Total = 20000;
    std::atomic<unsigned> destroyed{0}, producing{2}, consumed{0};
    std::array<std::atomic<bool>, Total> seen{};
    struct Item {
        unsigned id;
        std::atomic<unsigned>& destroyed;
        ~Item() { destroyed.fetch_add(1); }
    };
    AvPlayerQueue<std::unique_ptr<Item>> queue;
    std::vector<std::thread> threads;
    for (unsigned p = 0; p < 2; ++p) threads.emplace_back([&, p] {
        for (unsigned i = p; i < Total; i += 2) {
            queue.Push(std::unique_ptr<Item>(new Item{i, destroyed}));
            if ((i & 63) == 0) std::this_thread::yield();
        }
        producing.fetch_sub(1);
    });
    for (unsigned c = 0; c < 4; ++c) threads.emplace_back([&] {
        while (producing.load() || queue.Size()) {
            if (auto item = queue.Pop()) {
                assert(*item && (*item)->id < Total);
                assert(!seen[(*item)->id].exchange(true));
                consumed.fetch_add(1);
            } else std::this_thread::yield();
        }
    });
    threads.emplace_back([&] {
        while (producing.load()) {
            queue.Clear();
            std::this_thread::yield();
        }
    });
    for (auto& thread : threads) thread.join();
    queue.Clear();
    assert(destroyed == Total && consumed <= Total && queue.Size() == 0);
    assert(!queue.PopIf([](const auto&) { assert(false); return true; }));
}
