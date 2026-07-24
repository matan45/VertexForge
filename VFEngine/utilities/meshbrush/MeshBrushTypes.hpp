#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <cstdint>
#include <glm/glm.hpp>
#include "../terrain/BrushTypes.hpp"

namespace meshbrush
{
    struct MeshPaletteEntry
    {
        std::string meshPath;
        std::string materialPath;
        float weight = 1.0f;
        glm::vec2 scaleRange{0.8f, 1.2f};
        glm::vec2 rotationYRange{0.0f, 360.0f};
        bool randomRotationX = false;
        bool randomRotationZ = false;
        bool alignToNormal = false;
        float maxSlope = 90.0f; // degrees
        float yOffset = 0.0f; // Manual vertical offset from terrain surface
        bool useCollider = false; // Add static box collider per instance
        // Per-type render cull distance (0 = never cull / render-config category default).
        // Feeds MeshData.maxDrawDistance on spawn (VK-1578).
        float cullDistance = 0.0f;
    };

    // Complete in-memory description of an entity placed by the mesh brush.
    // instanceId is stable across undo/redo; the ECS entity handle is not.
    struct MeshBrushInstanceSpec
    {
        uint64_t instanceId = 0;
        uint32_t paletteIndex = 0;
        glm::vec3 worldPosition{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};
        std::string meshPath;
        std::string materialPath;
        bool useCollider = false;
        // Surface normal at placement time (already flipped upward). Persisted via
        // MeshBrushInstanceComponent so undo/redo respawn and scene-load rebuild keep it.
        glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};
        // Per-type render cull distance captured at placement (0 = never cull). Set on the
        // spawned MeshData.maxDrawDistance; recovered from MeshComponent on scene reload.
        float cullDistance = 0.0f;
    };

    // How a Paint stroke scatters instances (VK-1578).
    enum class MeshBrushPlacementMode : uint8_t
    {
        Spray = 0,  // Scatter many instances across the brush disk
        Single = 1  // Place one instance at the cursor (hero props)
    };

    struct MeshBrushParams
    {
        float radius = 5.0f;
        float density = 1.0f;
        float spacing = 2.0f;
        bool continuousMode = true;
        float positionJitter = 0.5f;
        terrain::BrushFalloff falloff = terrain::BrushFalloff::Smooth;
        // When true, Erase only removes instances of the currently-selected palette entry.
        bool eraseSelectedTypeOnly = false;

        // Placement masks — reject candidates failing these (VK-1578). Mirror of
        // VegetationBrushParams so both brushes share one mental model + noise helper.
        bool useSlopeMask = false;   // Limit placement by terrain slope
        float slopeMinCos = 0.0f;    // Min surface normal.y (1 = flat, 0 = vertical)
        float slopeMaxCos = 1.0f;    // Max surface normal.y
        bool useHeightMask = false;  // Limit placement by terrain height
        float heightMin = 0.0f;
        float heightMax = 100.0f;
        bool useNoiseMask = false;   // Clumping/scatter mask
        float noiseFrequency = 0.1f; // World-space noise frequency
        float noiseThreshold = 0.5f; // Reject below this noise value [0,1]
        uint32_t noiseSeed = 1337;

        MeshBrushPlacementMode placementMode = MeshBrushPlacementMode::Spray;

        void validate()
        {
            radius = std::max(radius, 0.1f);
            density = std::clamp(density, 0.01f, 10.0f);
            spacing = std::max(spacing, 0.1f);
            positionJitter = std::clamp(positionJitter, 0.0f, 1.0f);

            // Mask clamps: keep bands well-formed so degenerate UI input can't silently
            // reject every candidate (or collapse the noise lattice).
            slopeMinCos = std::clamp(slopeMinCos, 0.0f, 1.0f);
            slopeMaxCos = std::clamp(slopeMaxCos, 0.0f, 1.0f);
            if (slopeMinCos > slopeMaxCos) std::swap(slopeMinCos, slopeMaxCos);
            if (heightMin > heightMax) std::swap(heightMin, heightMax);
            noiseFrequency = std::max(noiseFrequency, 1e-4f);
            noiseThreshold = std::clamp(noiseThreshold, 0.0f, 1.0f);
        }
    };

    enum class MeshBrushMode : uint8_t
    {
        Paint = 0,
        Erase = 1
    };
}
