#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace bag_filter_v2_logic {

struct ItemDecisionInput {
    int position = -1;          // authoritative semantic bag Position, 0..99
    bool isEquip = false;
    bool bound = false;
    bool equipPointKnown = false;
    int equipPoint = -1;
    bool exactNameOverride = false;
};

inline bool ValidPosition(int position) {
    return position >= 0 && position <= 99;
}

// User-approved safety rule:
// - Bound item is NEVER dropped.
// - Exact user name override may opt an unlocked item into auto-drop.
// - Otherwise only unlocked Equip with a proven EquipPoint != 0 is dropped.
// - Unknown EquipPoint is fail-closed (keep).
inline bool ShouldDrop(const ItemDecisionInput& item) {
    if (!ValidPosition(item.position) || item.bound) return false;
    if (item.exactNameOverride) return true;
    return item.isEquip && item.equipPointKnown && item.equipPoint != 0;
}

inline bool IsWeapon(const ItemDecisionInput& item) {
    return item.isEquip && item.equipPointKnown && item.equipPoint == 0;
}

inline int RequiredScrollDepth(int position) {
    if (!ValidPosition(position)) return -1;
    if (position <= 41) return 0;
    if (position <= 83) return 1;
    return 2;
}

// Physical coordinate index inside the existing 42 F8 slot grid.
// Depth 0: pos 0..41 -> physical 0..41
// Depth 1: pos 42..83 -> physical 0..41
// Depth 2: pos 84..99 -> physical 21..36 (same overlap proven by V4 CP9 mapping)
inline int PhysicalIndex(int position) {
    if (!ValidPosition(position)) return -1;
    if (position <= 41) return position;
    if (position <= 83) return position - 42;
    return position - 63;
}

inline std::vector<int> NormalizeCandidates(std::vector<int> positions) {
    positions.erase(std::remove_if(positions.begin(), positions.end(), [](int p){ return !ValidPosition(p); }), positions.end());
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    return positions;
}

} // namespace bag_filter_v2_logic
