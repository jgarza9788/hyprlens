#pragma once

#include "Logic.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/helpers/math/Math.hpp>

#include <string_view>

inline constexpr std::string_view PLUGIN_NAME        = "hyprlens";
inline constexpr std::string_view PLUGIN_DESCRIPTION = "A live magnifier lens centered on the pointer";
inline constexpr std::string_view PLUGIN_AUTHOR      = "jgarza";
inline constexpr std::string_view PLUGIN_VERSION     = "0.1.0";

inline HANDLE PHANDLE = nullptr;

// Live settings, changed with `hyprctl hyprlens ...`.
struct SLens : Logic::Settings {
    bool enabled = false;
};

// What one frame needs to draw the lens.
struct SLensFrame {
    SLens    lens;
    Vector2D center;     // pointer, global logical: what the lens magnifies
    Vector2D lensCenter; // where the lens is drawn: the pointer, kept on screen
    float    alpha = 1.f; // fade
    float    scale = 1.f; // lens size relative to its set size (1 when settled)
    float    open  = 1.f; // 0 a solid dot .. 1 the ring has thinned and the view shows
};
