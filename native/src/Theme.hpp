#pragma once

#include <array>

namespace Theme {
    // The Omarchy theme's accent colour (rgb 0..1), re-read when colors.toml
    // changes. Falls back to Omarchy blue if the theme can't be read.
    std::array<float, 3> accent();
}
