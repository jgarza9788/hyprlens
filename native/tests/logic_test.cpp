// Unit tests for native/src/Logic.cpp (no Hyprland needed). `make test`.
#include "../src/Logic.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                                                                                                                                \
    do {                                                                                                                                                                           \
        if (!(cond)) {                                                                                                                                                             \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                                                            \
            ++failures;                                                                                                                                                            \
        } else                                                                                                                                                                     \
            std::printf("PASS %s\n", #cond);                                                                                                                                       \
    } while (0)

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

int main() {
    using namespace Logic;

    // parseFloat: plain numbers only
    CHECK(parseFloat("2.5") && near(*parseFloat("2.5"), 2.5f));
    CHECK(parseFloat("-3") && near(*parseFloat("-3"), -3.f));
    CHECK(!parseFloat(""));
    CHECK(!parseFloat("abc"));
    CHECK(!parseFloat("2x"));
    CHECK(!parseFloat("nan"));
    CHECK(!parseFloat("inf"));
    CHECK(!parseFloat(std::string(100, '1')));

    // named sizes
    CHECK(sizeIndex(250) == 0 && sizeIndex(400) == 1 && sizeIndex(600) == 2);
    CHECK(sizeIndex(10) == 0 && sizeIndex(5000) == 2);
    CHECK(sizeIndex(320) == 0 && sizeIndex(330) == 1); // nearest
    CHECK(std::string(sizeName(400)) == "medium");

    // apply: zoom
    Settings s;
    CHECK(apply(s, "zoom", "4").empty() && near(s.zoom, 4.f));
    CHECK(apply(s, "zoom", "1000").empty() && near(s.zoom, MAX_ZOOM));
    CHECK(apply(s, "zoom", "0.1").empty() && near(s.zoom, MIN_ZOOM));
    s.zoom = 3.f;
    CHECK(!apply(s, "zoom", "lots").empty() && near(s.zoom, 3.f)); // error leaves it alone

    // apply: size
    CHECK(apply(s, "size", "small").empty() && near(s.size, SIZES[0]));
    CHECK(apply(s, "size", "large").empty() && near(s.size, SIZES[2]));
    CHECK(apply(s, "size", "500").empty() && near(s.size, 500.f));
    CHECK(apply(s, "size", "1").empty() && near(s.size, MIN_SIZE));
    CHECK(apply(s, "size", "99999").empty() && near(s.size, MAX_SIZE));
    s.size = 400.f;
    CHECK(!apply(s, "size", "huge").empty() && near(s.size, 400.f));

    // apply: shape and smooth
    CHECK(apply(s, "shape", "square").empty() && !s.round);
    CHECK(apply(s, "shape", "circle").empty() && s.round);
    CHECK(!apply(s, "shape", "triangle").empty() && s.round);
    CHECK(apply(s, "smooth", "true").empty() && s.smooth);
    CHECK(apply(s, "smooth", "sharp").empty() && !s.smooth);
    CHECK(apply(s, "smooth", "smooth").empty() && s.smooth);
    CHECK(!apply(s, "smooth", "maybe").empty() && s.smooth);
    CHECK(!apply(s, "colour", "red").empty());

    // apply: outline style, colour, capture
    CHECK(apply(s, "style", "glow").empty() && s.style == "glow");
    CHECK(apply(s, "style", "none").empty() && s.style == "none");
    CHECK(apply(s, "style", "glass").empty() && s.style == "glass");
    CHECK(!apply(s, "style", "double").empty() && s.style == "glass"); // dropped style: an error, settings unchanged
    CHECK(!apply(s, "style", "neon").empty() && s.style == "glass"); // an error leaves it alone
    CHECK(styleIndex("ring") == 0 && styleIndex("glass") == 1 && styleIndex("glow") == 2 && styleIndex("none") == 3 && styleIndex("zzz") == 0);
    CHECK(apply(s, "outline", "#FF8800").empty() && s.outline == "#ff8800"); // normalised
    CHECK(apply(s, "outline", "00aaff").empty() && s.outline == "#00aaff");
    CHECK(apply(s, "outline", "accent").empty() && s.outline == "accent");
    CHECK(!apply(s, "outline", "#fff").empty() && s.outline == "accent");
    CHECK(!apply(s, "outline", "#gggggg").empty());
    CHECK(!apply(s, "width", "6").empty()); // the width setting was removed
    CHECK(apply(s, "capture", "hide").empty() && s.hideWhenSharing);
    CHECK(apply(s, "capture", "show").empty() && !s.hideWhenSharing);
    CHECK(!apply(s, "capture", "maybe").empty());
    CHECK(parseHexColor("#ff0000") && near((*parseHexColor("#ff0000"))[0], 1.f) && near((*parseHexColor("#ff0000"))[1], 0.f));
    CHECK(!parseHexColor("") && !parseHexColor("#12345") && !parseHexColor("#1234567"));
    s = Settings{};

    // zoomBy / stepSize / cycleSize
    s.zoom = 2.f;
    zoomBy(s, 1.25f);
    CHECK(near(s.zoom, 2.5f));
    zoomBy(s, 1000.f);
    CHECK(near(s.zoom, MAX_ZOOM));
    zoomBy(s, 1e-6f);
    CHECK(near(s.zoom, MIN_ZOOM));
    s.size = SIZES[1];
    stepSize(s, +1);
    CHECK(near(s.size, SIZES[2]));
    stepSize(s, +1);
    CHECK(near(s.size, SIZES[2])); // stops at large
    stepSize(s, -1);
    stepSize(s, -1);
    stepSize(s, -1);
    CHECK(near(s.size, SIZES[0])); // stops at small
    cycleSize(s);
    CHECK(near(s.size, SIZES[1]));
    cycleSize(s);
    cycleSize(s);
    CHECK(near(s.size, SIZES[0])); // wraps

    // the settings file round-trips
    Settings a;
    a.zoom = 6.f; a.size = 600.f; a.round = false; a.smooth = true;
    a.style = "glow"; a.outline = "#123abc"; a.hideWhenSharing = true;
    Settings b;
    fromFileJson(b, toFileJson(a));
    CHECK(near(b.zoom, 6.f) && near(b.size, 600.f) && !b.round && b.smooth);
    CHECK(b.style == "glow" && b.outline == "#123abc" && b.hideWhenSharing);
    // an old file without the outline settings keeps the defaults
    Settings old;
    fromFileJson(old, "{\"zoom\": 3, \"size\": 400, \"shape\": \"circle\", \"smooth\": false}");
    CHECK(old.style == "ring" && old.outline == "accent" && !old.hideWhenSharing);
    fromFileJson(old, "{\"style\": \"double\"}"); // a style that no longer exists: keep the default
    CHECK(old.style == "ring");
    fromFileJson(old, "{\"style\": \"star\", \"outline\": \"red\", \"capture\": 1}");
    CHECK(old.style == "ring" && old.outline == "accent" && !old.hideWhenSharing);

    // reading is forgiving: bad or missing values keep what's there
    Settings c;
    fromFileJson(c, "");
    CHECK(near(c.zoom, 2.5f) && near(c.size, 400.f) && c.round && !c.smooth);
    fromFileJson(c, "{\"zoom\": \"fast\", \"size\": 9999, \"shape\": \"star\", \"smooth\": 1}");
    CHECK(near(c.zoom, 2.5f) && near(c.size, MAX_SIZE) && c.round && !c.smooth);
    fromFileJson(c, "{\"zoom\":0,\"smooth\":true,\"shape\":\"square\"}");
    CHECK(near(c.zoom, MIN_ZOOM) && c.smooth && !c.round);
    fromFileJson(c, "{not json at all");
    CHECK(near(c.zoom, MIN_ZOOM));

    // rawValue
    CHECK(rawValue("{\"a\": 1, \"b\": \"x\"}", "a") == "1");
    CHECK(rawValue("{\"a\": 1, \"b\": \"x\"}", "b") == "x");
    CHECK(rawValue("{\"a\": 1}", "z").empty());

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
