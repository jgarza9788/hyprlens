#include "Theme.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {
    constexpr std::array<float, 3> FALLBACK{0x7a / 255.f, 0xa2 / 255.f, 0xf7 / 255.f};

    std::array<float, 3> s_accent = FALLBACK;
    struct timespec      s_mtime{};
    std::string          s_path;

    std::string colorsPath() {
        const char*       home  = std::getenv("HOME");
        const std::string base  = home ? home : "";
        const std::string state = base + "/.local/state/omarchy/current/theme/colors.toml";
        struct stat       st{};
        return stat(state.c_str(), &st) == 0 ? state : base + "/.config/omarchy/current/theme/colors.toml";
    }

    // `accent = "#rrggbb"` (quotes optional) from colors.toml.
    bool parse(const std::string& path, std::array<float, 3>& out) {
        std::ifstream f(path);
        std::string   line;
        for (int n = 0; n < 512 && std::getline(f, line); ++n) {
            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = line.substr(0, eq);
            key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c); }), key.end());
            if (key != "accent")
                continue;
            const auto hash = line.find('#', eq);
            if (hash == std::string::npos || line.size() < hash + 7)
                return false;
            unsigned v = 0;
            for (int i = 1; i <= 6; ++i) {
                const char c = line[hash + i];
                if (!std::isxdigit((unsigned char)c))
                    return false;
                v = v * 16 + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
            }
            out = {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f};
            return true;
        }
        return false;
    }
}

std::array<float, 3> Theme::accent() {
    const std::string path = colorsPath();
    struct stat       st{};
    if (stat(path.c_str(), &st) != 0)
        return s_accent;
    if (path == s_path && st.st_mtim.tv_sec == s_mtime.tv_sec && st.st_mtim.tv_nsec == s_mtime.tv_nsec)
        return s_accent;
    s_path  = path;
    s_mtime = st.st_mtim;
    std::array<float, 3> c = FALLBACK;
    s_accent = parse(path, c) ? c : FALLBACK;
    return s_accent;
}
