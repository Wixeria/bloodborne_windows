// bbport: host threads that may call guest code (AvPlayer allocator callbacks).
// Each thread gets a guest TCB (GS base, TLS) from the C runtime before running.
#pragma once
#include <functional>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
#include "common/types.h"

extern "C" void runtime_thread_attach_host(const char* name);

namespace Libraries::Kernel {
class Thread {
public:
    Thread() = default;
    ~Thread() { Stop(); }
    void Run(std::function<void(std::stop_token)>&& func) {
        Stop();
        std::scoped_lock lock{mutex};
        thread = std::jthread([func = std::move(func)](std::stop_token stop) {
            runtime_thread_attach_host("bb:hle");
            func(stop);
        });
        worker_id = thread.get_id();
        stop_source = thread.get_stop_source();
    }
    // AvPlayer workers call Join on themselves while Stop/EOF may also join
    // them externally. Leave the handle owned until an external caller joins:
    // detaching here races that join and can outlive the source's buffers.
    void Join() {
        std::unique_lock lock{mutex};
        if (worker_id == std::this_thread::get_id()) return;
        if (joining) {
            joined.wait(lock, [this] { return !joining; });
            return;
        }
        if (!thread.joinable()) return;
        joining = true;
        auto owned = std::move(thread);
        lock.unlock();
        owned.join();
        lock.lock();
        worker_id = {};
        joining = false;
        lock.unlock();
        joined.notify_all();
    }
    bool Joinable() const {
        std::scoped_lock lock{mutex};
        return joining || thread.joinable();
    }
    void Stop() {
        std::stop_source source;
        {
            std::scoped_lock lock{mutex};
            source = stop_source;
        }
        source.request_stop();
        Join();
    }

private:
    std::jthread thread;
    std::stop_source stop_source;
    std::thread::id worker_id;
    mutable std::mutex mutex;
    std::condition_variable joined;
    bool joining = false;
};
} // namespace Libraries::Kernel
