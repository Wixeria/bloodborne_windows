// bbport: thresholds use the same current driver budget as measured heap usage.
#pragma once
#include <cstdint>
#include <optional>

namespace VideoCore {
struct MemoryThresholds {
    std::uint64_t trigger, pressure, critical;
};

inline constexpr std::optional<MemoryThresholds> CurrentMemoryThresholds(std::uint64_t budget) {
    if (budget == 0) return std::nullopt; // driver unavailable: retain fallback thresholds
    const auto percent = [budget](std::uint64_t value) {
        // Do not overflow when a large heap is reported.
        return (budget / 100) * value + (budget % 100) * value / 100;
    };
    return MemoryThresholds{percent(70), percent(85), percent(95)};
}
} // namespace VideoCore
