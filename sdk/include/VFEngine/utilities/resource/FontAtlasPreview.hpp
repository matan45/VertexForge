#pragma once

#include "Types.hpp"

namespace resource
{
    // VK-1634: flatten a font atlas of any FontAtlasFormat into a 4-channel RGBA image for
    // display. Every distance-field format is reconstructed the way the runtime shaders
    // reconstruct it (single-channel field, or median RGB for MTSDF) and written out as a
    // white glyph with the coverage in alpha; a colour-emoji RGBA_32 atlas is passed
    // through untouched.
    //
    // The preview bakes 1:1 - one atlas texel per preview pixel - so the shaders'
    // screen-space band collapses to the atlas-native one. That is why this can be a plain
    // CPU function with no notion of on-screen scale.
    //
    // Lives in Utilities rather than in the editor window that draws it so it is reachable
    // from the test suite, which links Utilities but not the Editor executable.
    //
    // Throws std::runtime_error if the atlas dimensions, format, or pixel-buffer size are
    // inconsistent with one another.
    TextureData fontAtlasToPreviewRGBA(const FontData& fontData);
}
