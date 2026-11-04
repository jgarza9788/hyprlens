#pragma once

// Pure logic for hyprlens: no Hyprland headers, so native/tests can build it on
// its own. Setting limits and validation, the three named sizes, and the small
// JSON file the settings live in.

#include <array>
#include <optional>
#include <string>

namespace Logic {
    inline constexpr float MIN_ZOOM = 1.25f, MAX_ZOOM = 24.f;
    inline constexpr float MIN_SIZE = 120.f, MAX_SIZE = 1400.f;
    inline constexpr float SIZES[3] = {250.f, 400.f, 600.f}; // small, medium, large

    inline constexpr float RIM = 3.f; // outline thickness, logical px (glass draws it thinner)

    // Outline styles, in the order the shader numbers them.
    inline constexpr const char* STYLES[] = {"ring", "glass", "glow", "none"};
    inline constexpr int         N_STYLES = 4;

    struct Settings {
        float       zoom    = 2.5f;     // magnification, MIN_ZOOM .. MAX_ZOOM
        float       size    = 400.f;    // lens width/height, logical px
        bool        round   = true;     // circle (true) or rounded square
        bool        smooth  = false;    // smooth (true) or sharp pixels
        std::string style   = "ring";   // outline: ring | glass | glow | none
        std::string outline = "accent"; // outline colour: "accent" (the theme's) or #rrggbb
        bool        hideWhenSharing = false; // leave the lens out while the screen is shared or recorded
    };

    // Index of a style name (0 ring, 1 glass, 2 glow, 3 none); 0 for anything unknown.
    int styleIndex(const std::string& style);

    // "#rrggbb" or "rrggbb" -> rgb 0..1.
    std::optional<std::array<float, 3>> parseHexColor(const std::string& s);

    // A plain decimal number; nothing else (no trailing junk, NaN or inf).
    std::optional<float> parseFloat(const std::string& s);

    // Which of the three named sizes `size` is closest to (0 small, 1 medium, 2 large).
    int         sizeIndex(float size);
    const char* sizeName(float size);

    // Set one setting by name: zoom <number>, size <small|medium|large|px>,
    // shape <circle|square>, smooth <true|false|smooth|sharp>,
    // style <ring|glass|glow|none>, outline <accent|#rrggbb>,
    // capture <show|hide>. Values are
    // clamped to range. Returns "" on success, else a message; on error the
    // settings are left as they were.
    std::string apply(Settings& s, const std::string& key, const std::string& value);

    void zoomBy(Settings& s, float factor);
    void stepSize(Settings& s, int delta); // bigger (+1) / smaller (-1): stops at the ends
    void cycleSize(Settings& s);           // small -> medium -> large -> small

    // The settings file: a flat JSON object. Reading ignores missing or bad
    // values and keeps what `s` already holds.
    std::string toFileJson(const Settings& s);
    void        fromFileJson(Settings& s, const std::string& text);

    // Raw text of "key": in a flat JSON object ("" if absent); quotes removed.
    std::string rawValue(const std::string& text, const std::string& key);
}
