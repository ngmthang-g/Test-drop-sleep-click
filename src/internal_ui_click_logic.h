#pragma once

#include <array>

namespace internal_ui_click_logic {

constexpr int kLeftButton = 0;

enum class DispatchPhase { Press, Release };
struct DispatchStep { DispatchPhase phase; const char* methodName; int parameterCount; };

inline constexpr std::array<DispatchStep, 2> DispatchPlan() {
    return {{
        {DispatchPhase::Press, "TryClickUI", 2},
        {DispatchPhase::Release, "EndUIDrag", 1},
    }};
}

inline constexpr bool IsNormalizedCoordinate(int value, int scale) {
    return scale > 0 && value >= 0 && value < scale;
}

} // namespace internal_ui_click_logic
