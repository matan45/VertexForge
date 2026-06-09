#pragma once

#include <cstdint>

namespace core
{
    // Shared bindless descriptor-array constants. Used by both the mesh-material
    // bindless table (render::gpudriven) and the UI bindless table (render::ui).
    constexpr uint32_t MAX_BINDLESS_TEXTURES = 16384;
    constexpr uint32_t INVALID_TEXTURE_INDEX = 0xFFFFFFFF;
}
