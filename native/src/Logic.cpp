#include "Logic.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

namespace Logic {

std::optional<float> parseFloat(const std::string& s) {
    if (s.empty() || s.size() > 32)
        return std::nullopt;
    try {
        size_t      n = 0;
        const float v = std::stof(s, &n);
        if (n == s.size() && std::isfinite(v))
            return v;
    } catch (...) {}
    return std::nullopt;
}

int sizeIndex(float size) {
    int best = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs(SIZES[i] - size) < std::abs(SIZES[best] - size))
            best = i;
    return best;
}

const char* sizeName(float size) {
    static constexpr const char* NAMES[] = {"small", "medium", "large"};
    return NAMES[sizeIndex(size)];
}

int styleIndex(const std::string& style) {
    for (int i = 0; i < N_STYLES; ++i)
        if (style == STYLES[i])
            return i;
    return 0;
}

std::optional<std::array<float, 3>> parseHexColor(const std::string& in) {
    std::string s = in;
    if (!s.empty() && s[0] == '#')
        s.erase(0, 1);
    if (s.size() != 6)
        return std::nullopt;
    unsigned v = 0;
    for (const char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return std::nullopt;
        v = v * 16 + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
    }
    return std::array<float, 3>{((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f};
}

std::string apply(Settings& s, const std::string& key, const std::string& value) {
    if (key == "zoom") {
        const auto v = parseFloat(value);
        if (!v)
            return "zoom must be a number";
        s.zoom = std::clamp(*v, MIN_ZOOM, MAX_ZOOM);
    } else if (key == "size") {
        if (value == "small")
            s.size = SIZES[0];
        else if (value == "medium")
            s.size = SIZES[1];
        else if (value == "large")
            s.size = SIZES[2];
        else if (const auto v = parseFloat(value))
            s.size = std::clamp(*v, MIN_SIZE, MAX_SIZE);
        else
            return "size must be small, medium, large or a pixel count";
    } else if (key == "shape") {
        if (value != "circle" && value != "square")
            return "shape must be circle or square";
        s.round = value == "circle";
    } else if (key == "smooth") {
        if (value != "true" && value != "false" && value != "smooth" && value != "sharp")
            return "smooth must be true or false";
        s.smooth = value == "true" || value == "smooth";
    } else if (key == "style") {
        bool known = false;
        for (const auto* name : STYLES)
            known = known || value == name;
        if (!known)
            return "style must be ring, glass, glow or none";
        s.style = value;
    } else if (key == "outline") {
        if (value == "accent")
            s.outline = value;
        else if (const auto c = parseHexColor(value); c) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "#%02x%02x%02x", (int)std::lround((*c)[0] * 255), (int)std::lround((*c)[1] * 255), (int)std::lround((*c)[2] * 255));
            s.outline = buf;
        } else
            return "outline must be accent or a colour like #ff8800";
    } else if (key == "capture") {
        if (value != "show" && value != "hide")
            return "capture must be show or hide";
        s.hideWhenSharing = value == "hide";
    } else
        return "unknown setting " + key;
    return "";
}

void zoomBy(Settings& s, float factor) {
    s.zoom = std::clamp(s.zoom * factor, MIN_ZOOM, MAX_ZOOM);
}

void stepSize(Settings& s, int delta) {
    s.size = SIZES[std::clamp(sizeIndex(s.size) + delta, 0, 2)];
}

void cycleSize(Settings& s) {
    s.size = SIZES[(sizeIndex(s.size) + 1) % 3];
}

std::string rawValue(const std::string& text, const std::string& key) {
    const auto k = text.find("\"" + key + "\"");
    if (k == std::string::npos)
        return "";
    auto i = text.find(':', k);
    if (i == std::string::npos)
        return "";
    ++i;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t'))
        ++i;
    auto e = i;
    while (e < text.size() && text[e] != ',' && text[e] != '}' && text[e] != '\n')
        ++e;
    std::string v = text.substr(i, e - i);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\r' || v.back() == '\t'))
        v.pop_back();
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
        v = v.substr(1, v.size() - 2);
    return v;
}

std::string toFileJson(const Settings& s) {
    char buf[360];
    std::snprintf(buf, sizeof buf,
                  "{\n  \"zoom\": %.3g,\n  \"size\": %.0f,\n  \"shape\": \"%s\",\n  \"smooth\": %s,\n  \"style\": \"%s\",\n  \"outline\": \"%s\",\n  \"capture\": \"%s\"\n}\n",
                  s.zoom, s.size, s.round ? "circle" : "square", s.smooth ? "true" : "false", s.style.c_str(), s.outline.c_str(), s.hideWhenSharing ? "hide" : "show");
    return buf;
}

void fromFileJson(Settings& s, const std::string& text) {
    if (const auto v = parseFloat(rawValue(text, "zoom")))
        s.zoom = std::clamp(*v, MIN_ZOOM, MAX_ZOOM);
    if (const auto v = parseFloat(rawValue(text, "size")))
        s.size = std::clamp(*v, MIN_SIZE, MAX_SIZE);
    if (const auto v = rawValue(text, "shape"); v == "circle" || v == "square")
        s.round = v == "circle";
    if (const auto v = rawValue(text, "smooth"); v == "true" || v == "false")
        s.smooth = v == "true";
    // The rest go through apply(), so a bad value is ignored the same way.
    for (const char* key : {"style", "outline", "capture"}) {
        const auto v = rawValue(text, key);
        if (!v.empty())
            apply(s, key, v);
    }
}

} // namespace Logic
