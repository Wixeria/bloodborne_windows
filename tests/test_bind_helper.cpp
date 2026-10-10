#include <cassert>
#include <chrono>
#include <thread>
#include "video_core/renderer_vulkan/vk_bind_helper.h"

struct State {
    unsigned completed = 0;
    unsigned expected = 0;
};

static void execute(void* context) {
    auto& state = *static_cast<State*>(context);
    assert(Vulkan::BindHelper::OnHelper());
    assert(state.completed == state.expected);
    ++state.completed;
}

int main() {
    assert(!Vulkan::BindHelper::OnHelper());
    {
        Vulkan::BindHelper disabled(false);
        assert(!disabled.Available());
        assert(!disabled.Active());
        disabled.Join();
    }
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        State state;
        Vulkan::BindHelper helper(true);
        assert(helper.Available());
        for (unsigned task = 0; task < 100; ++task) {
            // Exercise both spinning and atomic-wait wakeup paths.
            if (task % 10 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            state.expected = task;
            helper.Fork(execute, &state);
            assert(helper.Active());
            helper.Join();
            assert(!helper.Active());
            assert(state.completed == task + 1);
            assert(helper.Available());
            helper.Join();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        // Destruction must wake a sleeping worker and join before freeing its atomics.
    }
    assert(!Vulkan::BindHelper::OnHelper());
}
