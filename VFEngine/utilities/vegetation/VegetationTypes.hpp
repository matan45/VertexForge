#pragma once

#include "../terrain/BrushTypes.hpp"
#include <cstdint>
#include <algorithm>
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace vegetation
{
    // Billboard rendering mode
    enum class BillboardMode : uint8_t
    {
        Cross = 0,       // Two perpendicular quads (X shape)
        CameraFacing = 1 // Always faces camera
    };

    // Brush mode
    enum class VegetationBrushType : uint8_t
    {
        Paint = 0,
        Erase = 1
    };

    // A single placed billboard instance
    struct BillboardInstance
    {
        glm::vec3 position{0.0f};    // World-space position (snapped to terrain height)
        float rotation = 0.0f;       // Y-axis rotation in radians
        float scale = 1.0f;          // Uniform scale factor
        uint32_t paletteEntryIndex = 0; // Index into billboard palette
        float windPhase = 0.0f;      // Random wind phase [0,1]
    };

    // Billboard palette entry (texture + settings)
    struct BillboardPaletteEntry
    {
        std::string texturePath;                     // Path to .vfImage file
        float weight = 1.0f;                         // For weighted random selection
        glm::vec2 scaleRange{0.2f, 0.4f};           // Min/max random scale
        BillboardMode mode = BillboardMode::Cross;   // Cross or camera-facing
        bool visible = true;                         // Toggle rendering on/off
        bool paintEnabled = false;                   // Include in paint brush
        uint32_t bindlessTextureIndex = 0xFFFFFFFF;  // Resolved at runtime
    };

    // Brush parameters for instance placement
    struct VegetationBrushParams
    {
        float radius = 5.0f;          // Brush radius in world units
        float spacing = 0.5f;         // Min distance between instances
        float density = 1.0f;         // Instances per brush application
        float positionJitter = 0.5f;  // Random offset within spacing
        terrain::BrushFalloff falloff = terrain::BrushFalloff::Smooth;
    };

    // Max billboard palette entries
    static constexpr uint32_t MAX_BILLBOARD_ENTRIES = 8;
}
