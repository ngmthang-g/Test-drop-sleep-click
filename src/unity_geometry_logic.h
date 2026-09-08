#pragma once

#include <array>

namespace unity_geometry_logic {

enum class ImageSlot {
    CoreModule,
    UiModule,
    LegacyUnity,
};

enum class GeometryClass {
    RectTransform,
    Transform,
    GameObject,
    RectTransformUtility,
    Screen,
};

struct SearchPlan {
    const char* className;
    std::array<ImageSlot, 3> images;
};

inline constexpr SearchPlan PlanFor(GeometryClass role) {
    switch (role) {
        case GeometryClass::RectTransform:
            return {"RectTransform", {ImageSlot::CoreModule, ImageSlot::LegacyUnity, ImageSlot::UiModule}};
        case GeometryClass::Transform:
            return {"Transform", {ImageSlot::CoreModule, ImageSlot::LegacyUnity, ImageSlot::UiModule}};
        case GeometryClass::GameObject:
            return {"GameObject", {ImageSlot::CoreModule, ImageSlot::LegacyUnity, ImageSlot::UiModule}};
        case GeometryClass::RectTransformUtility:
            return {"RectTransformUtility", {ImageSlot::UiModule, ImageSlot::CoreModule, ImageSlot::LegacyUnity}};
        case GeometryClass::Screen:
            return {"Screen", {ImageSlot::CoreModule, ImageSlot::LegacyUnity, ImageSlot::UiModule}};
    }
    return {"", {ImageSlot::CoreModule, ImageSlot::UiModule, ImageSlot::LegacyUnity}};
}

inline constexpr const wchar_t* ClassLabel(GeometryClass role) {
    switch (role) {
        case GeometryClass::RectTransform: return L"RectTransform";
        case GeometryClass::Transform: return L"Transform";
        case GeometryClass::GameObject: return L"GameObject";
        case GeometryClass::RectTransformUtility: return L"RectTransformUtility";
        case GeometryClass::Screen: return L"Screen";
    }
    return L"Unknown";
}

} // namespace unity_geometry_logic
