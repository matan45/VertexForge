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

    // Provenance of a placed billboard instance (VK-1581 procedural scatter).
    // Painted = hand-placed by the vegetation brush; Procedural = emitted by a scatter
    // bake. Regenerate replaces only Procedural instances, leaving Painted untouched.
    enum class InstanceSource : uint8_t
    {
        Painted = 0,
        Procedural = 1
    };

    // Brush mode
    enum class VegetationBrushType : uint8_t
    {
        Paint = 0,
        Erase = 1
    };

    // How the paint brush scatters instances
    enum class VegetationPlacementMode : uint8_t
    {
        Spray = 0,  // Scatter many instances across the brush disk (grass/foliage)
        Single = 1  // Place a single instance at the cursor (hero props)
    };

    // A single placed billboard instance
    struct BillboardInstance
    {
        glm::vec3 position{0.0f};    // World-space position (snapped to terrain height)
        float rotation = 0.0f;       // Y-axis rotation in radians
        float scale = 1.0f;          // Uniform scale factor
        uint32_t paletteEntryIndex = 0; // Index into billboard palette
        float windPhase = 0.0f;      // Random wind phase [0,1]
        float heightScale = 1.0f;    // Per-instance height multiplier (variation)
        float tint = 1.0f;           // Per-instance brightness/tint multiplier [~0.5,1.5]
        glm::vec3 normal{0.0f, 1.0f, 0.0f}; // Terrain surface normal (for align-to-normal)
        InstanceSource source = InstanceSource::Painted; // Painted vs procedural (CPU-only; not uploaded to GPU)
    };

    // Billboard palette entry (texture + settings)
    struct BillboardPaletteEntry
    {
        std::string texturePath;                     // Path to .vfImage file
        float weight = 1.0f;                         // For weighted random selection
        glm::vec2 scaleRange{0.2f, 0.4f};           // Min/max random scale
        glm::vec2 heightRange{1.0f, 1.0f};          // Min/max random height multiplier
        float tintJitter = 0.0f;                     // Per-instance brightness jitter amount [0,1]
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

        // Placement masks (reject candidates failing these)
        bool useSlopeMask = false;    // Limit placement by terrain slope
        float slopeMinCos = 0.0f;     // Min surface normal.y (1=flat, 0=vertical)
        float slopeMaxCos = 1.0f;     // Max surface normal.y
        bool alignToNormal = false;   // Tilt instances to the terrain normal
        bool useHeightMask = false;   // Limit placement by terrain height
        float heightMin = 0.0f;
        float heightMax = 100.0f;

        // Noise/scatter mask (clumping)
        bool useNoiseMask = false;
        float noiseFrequency = 0.1f;  // World-space noise frequency
        float noiseThreshold = 0.5f;  // Reject below this noise value [0,1]
        uint32_t noiseSeed = 1337;

        // Flow / placement mode
        VegetationPlacementMode placementMode = VegetationPlacementMode::Spray;
        float flowRate = 0.0f;        // 0=throttle by spacing; >0 = instances/sec while held (airbrush)

        // Layer-aware avoidance
        bool avoidOtherLayers = false; // Reject candidates near a different palette layer
        float layerAvoidRadius = 0.5f; // Min distance to a different layer
    };

    // Max billboard palette entries
    static constexpr uint32_t MAX_BILLBOARD_ENTRIES = 8;
}
