#pragma once

#include "g1/Image.hpp"

namespace g1 {

// Two lossless Scale2x passes, as used by the PC port's optional Modern
// Scale4x texture mode. The expanded image uses only the source RGBA colours,
// including their unmodified alpha words. No content-aware reconstruction,
// synthetic colours, GPU feedback or game-state mutation is involved.
//
// Only a bounded source texture (max 1024 x 1024, 4 MiB) may be expanded;
// output dimensions are exactly four times the input and at most 64 MiB.
// Intended for the original 2D character/decorative sprite atlases, NOT the
// 32-cell terrain atlas, UI font sheets or transparent 3D material textures.
RgbaImage scale4x_rgba(const RgbaImage& image);

} // namespace g1
