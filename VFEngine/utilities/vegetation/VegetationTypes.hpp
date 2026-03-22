#pragma once

#include "../terrain/BrushTypes.hpp"
#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace vegetation
{
    enum class DensityBrushType : uint8_t
    {
        Paint = 0,
        Erase = 1,
        Smooth = 2,
        Fill = 3
    };

    enum class VegetationType : uint8_t
    {
        Billboard = 0,
        Count = 1
    };

    // Max billboard entries per terrain - each gets its own density map slot
    static constexpr uint32_t MAX_BILLBOARD_ENTRIES = 8;
    static constexpr uint32_t VEGETATION_TYPE_COUNT = MAX_BILLBOARD_ENTRIES;

    enum class BillboardMode : uint8_t
    {
        Cross = 0,       // Two perpendicular quads (X shape) - good for bushes, grass clumps
        CameraFacing = 1 // Always faces camera - good for flowers, small decals
    };

    struct BillboardPaletteEntry
    {
        std::string texturePath;                     // Path to .vfImage file
        float weight = 1.0f;                         // For weighted random selection
        glm::vec2 scaleRange{0.2f, 0.4f};           // Min/max random scale
        float densityMultiplier = 1.0f;              // Per-entry density
        BillboardMode mode = BillboardMode::Cross;   // Cross or camera-facing
        bool visible = true;                         // Toggle rendering on/off
        bool paintEnabled = false;                   // Include in paint brush (user must enable)
        uint32_t bindlessTextureIndex = 0xFFFFFFFF;  // Resolved at runtime
    };

    struct VegetationTypeConfig
    {
        VegetationType type = VegetationType::Billboard;
        float scaleMin = 0.5f;
        float scaleMax = 1.5f;
        float rotationRandomization = 1.0f;  // 0=aligned, 1=fully random
        float slopeLimit = 0.7f;
        float densityMultiplier = 1.0f;
        float fadeStartDistance = 80.0f;
        float fadeEndDistance = 120.0f;
        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
        std::vector<BillboardPaletteEntry> billboardEntries; // Billboard palette
    };

    struct MixedBrushConfig
    {
        bool enabled = false;
        std::array<float, VEGETATION_TYPE_COUNT> ratios = {};

        void normalize()
        {
            float sum = 0.0f;
            for (float r : ratios) sum += r;
            if (sum > 0.0f)
            {
                for (float& r : ratios) r /= sum;
            }
            else
            {
                ratios[0] = 1.0f;
                for (size_t i = 1; i < ratios.size(); ++i) ratios[i] = 0.0f;
            }
        }
    };

    struct DensityBrushParams
    {
        float radius = 5.0f;
        float strength = 1.0f;
        float opacity = 1.0f;
        terrain::BrushFalloff falloff = terrain::BrushFalloff::Smooth;
        terrain::BrushShape shape = terrain::BrushShape::Circle;

        void validate()
        {
            radius = std::max(radius, 0.1f);
            strength = std::clamp(strength, 0.0f, 100.0f);
            opacity = std::clamp(opacity, 0.0f, 1.0f);
        }
    };
}
