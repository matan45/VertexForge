#pragma once
#include <cstdint>
#include <string>
#include <type_traits>
#include <glm/glm.hpp>

namespace foliage
{
    // Far-render strategy per foliage type. Only LOD3Cutoff is wired now
    // (cull past endCullDistance). The impostor system was removed engine-wide;
    // BillboardImposter/HLODProxy are reserved for the VK-1583 far-strategy spike.
    enum class FoliageFarMode : uint8_t
    {
        LOD3Cutoff        = 0,
        BillboardImposter = 1, // reserved (VK-1583)
        HLODProxy         = 2, // reserved (VK-1583)
    };

    // Per-instance flag bits (FoliageInstance.flags, uint16_t). Namespaced-constexpr
    // to match the engine ObjectFlags idiom (GPUDrivenTypes.hpp).
    namespace FoliageInstanceFlags
    {
        constexpr uint16_t None          = 0;
        constexpr uint16_t Tilt          = 1u << 0; // random tilt applied
        constexpr uint16_t Collider      = 1u << 1; // has proximity collider  (VK-1584)
        constexpr uint16_t NavContribute = 1u << 2; // contributes to navmesh  (VK-1584)
    }

    // Palette cap. typeIndex is u16 (headroom) but the palette is capped at 64.
    constexpr uint16_t MAX_FOLIAGE_TYPES = 64;

    // Packed, entity-free per-plant record. Stored by value in TerrainTile's vector,
    // so it crosses the Terrain DLL ABI boundary — FROZEN at 56 bytes from day one.
    // Layout (offsets): pos@0(12) rotationY@12(4) scale@16(12) typeIndex@28(2)
    //                   flags@30(2) normal@32(12) windPhase@44(4) tint@48(4) seed@52(4)
    struct FoliageInstance
    {
        glm::vec3 position{0.0f};             // world-space (snapped to terrain height)
        float     rotationY = 0.0f;           // Y-axis rotation, radians
        glm::vec3 scale{1.0f};                // non-uniform scale
        uint16_t  typeIndex = 0;              // index into FoliageType palette (< MAX_FOLIAGE_TYPES)
        uint16_t  flags     = FoliageInstanceFlags::None;
        glm::vec3 normal{0.0f, 1.0f, 0.0f};   // terrain surface normal (align-to-normal)
        float     windPhase = 0.0f;           // [0,1] per-instance wind phase
        uint32_t  tint      = 0xFFFFFFFFu;    // packed RGBA8 -> albedoOverride.rgb (VK-1573)
        uint32_t  seed      = 0;              // deterministic per-instance seed
    };

    static_assert(sizeof(FoliageInstance) == 56, "FoliageInstance is a frozen 56-byte cross-DLL ABI struct");
    static_assert(alignof(FoliageInstance) == 4, "FoliageInstance must stay 4-byte aligned (no forced-aligned glm gentypes)");
    static_assert(std::is_trivially_copyable_v<FoliageInstance>, "FoliageInstance must stay trivially copyable (packed store / future binary IO)");
    static_assert(std::is_standard_layout_v<FoliageInstance>,   "FoliageInstance must stay standard-layout");

    // Palette entry (superset of BillboardPaletteEntry + MeshPaletteEntry + new fields).
    // Has std::string members -> NOT ABI-frozen; cheap to extend in later stories.
    struct FoliageType
    {
        // Identity
        std::string meshPath;                    // .vfMesh
        std::string materialPath;                // material / .vfMatInstance

        // Selection
        float weight       = 1.0f;               // weighted random pick
        float densityScale = 1.0f;               // per-type density multiplier (VK-1582)
        bool  affectedByDensityScale = true;     // VK-1582: opt out of the GLOBAL foliage density
                                                 // scale (Unity parity) for gameplay-relevant foliage

        // Placement transform
        glm::vec2 scaleRange{0.8f, 1.2f};        // uniform scale min/max
        glm::vec2 heightRange{1.0f, 1.0f};       // extra Y multiplier min/max
        glm::vec2 rotationYRange{0.0f, 360.0f};  // degrees
        float     randomTilt   = 0.0f;           // max random tilt off surface, degrees
        bool      alignToNormal = false;

        // Placement masks
        float     minSlopeDeg = 0.0f;
        float     maxSlopeDeg = 90.0f;
        glm::vec2 altitudeRange{-100000.0f, 100000.0f}; // world-Y min/max

        // Rendering
        float          startCullDistance = 0.0f;   // begin fade
        float          endCullDistance   = 200.0f; // fully culled
        bool           castShadow        = true;
        FoliageFarMode farMode           = FoliageFarMode::LOD3Cutoff;

        // Wind (global WindSystem drives sway today; per-type params reserved for VK-1580)
        bool  receiveWind   = false;
        float windStrength  = 1.0f;
        float windStiffness = 1.0f;

        // Gameplay
        bool collision     = false; // static collider per instance (VK-1584)
        bool navContribute = false; // (VK-1584)

        // Editor
        bool visible      = true;
        bool paintEnabled = true;
    };
}
