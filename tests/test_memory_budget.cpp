#include <cassert>
#include <cstdint>
#include <limits>
#include <initializer_list>
#include "video_core/texture_cache/memory_budget.h"

int main() {
    using VideoCore::CurrentMemoryThresholds;
    constexpr std::uint64_t mib = 1024 * 1024, gib = mib * 1024;
    assert(!CurrentMemoryThresholds(0));
    for (const auto budget : {mib, 512 * mib, gib, 6 * gib, 32 * gib,
                             std::numeric_limits<std::uint64_t>::max()}) {
        const auto thresholds = *CurrentMemoryThresholds(budget);
        assert(thresholds.trigger <= thresholds.pressure);
        assert(thresholds.pressure <= thresholds.critical);
        assert(thresholds.critical < budget);
    }
    const auto normal = *CurrentMemoryThresholds(5000 * mib);
    assert(normal.trigger == 3500 * mib && normal.pressure == 4250 * mib);
    assert(normal.critical == 4750 * mib);
    // The measured 2.4-GB clinic usage is not pressure against a 5.2-GB budget.
    assert(2513952768ull < CurrentMemoryThresholds(5487198208ull)->pressure);
    // Respond when other applications/OS reduce the driver's allowance.
    const auto reduced = *CurrentMemoryThresholds(2500 * mib);
    assert(2400 * mib >= reduced.critical);
    assert(2400 * mib < normal.pressure);
    // Odd byte counts retain deterministic rounding, not whole-MiB quantization.
    assert(CurrentMemoryThresholds(101)->trigger == 70);
    assert(CurrentMemoryThresholds(101)->pressure == 85);
    assert(CurrentMemoryThresholds(101)->critical == 95);
}
