// CPU-side contract tests for VK-1415 per-object render layers + per-camera culling masks.
//
// The render layer (index 0-31) is packed into GPUObjectData.flags bits 18-22 on the CPU
// (MergedMeshBufferScene.cpp / GPUObjectStreamManager.cpp) and the GPU cull shader
// (resources/shaders/gpudriven/gpu_cull_lod.glsl) expands it to a bit and ANDs it with the
// camera's cullingMask (GPUCameraData.cullExtra.x):
//
//     obj.flags |= (renderLayer & LayerMask) << LayerShift;          // CPU pack
//     uint objLayerBit = 1u << ((obj.flags >> LAYER_SHIFT) & LAYER_MASK);  // shader
//     if ((objLayerBit & camera.cullExtra.x) == 0u) return;               // shader early-out
//
// These tests mirror that contract. The constants must stay in lockstep with
// ObjectFlags::LayerShift/LayerMask (GPUDrivenTypes.hpp) and LAYER_SHIFT/LAYER_MASK
// (gpu_draw_functions.glsl). The byte-size of GPUCameraData (544) is pinned by a
// static_assert in CameraTypes.hpp, enforced at graphics compile time.

#include "doctest.h"
#include <cstdint>
#include <initializer_list>

namespace
{
    constexpr uint32_t LayerShift = 18;   // mirrors ObjectFlags::LayerShift
    constexpr uint32_t LayerMask  = 0x1Fu; // mirrors ObjectFlags::LayerMask

    // CPU pack (mirrors MergedMeshBufferScene::populateObjectData).
    uint32_t packLayer(uint32_t existingFlags, uint32_t layerIndex)
    {
        return existingFlags | ((layerIndex & LayerMask) << LayerShift);
    }

    // Shader-side visibility test (mirrors gpu_cull_lod.glsl early-out).
    bool visibleToCamera(uint32_t objFlags, uint32_t cullingMask)
    {
        uint32_t objLayerBit = 1u << ((objFlags >> LayerShift) & LayerMask);
        return (objLayerBit & cullingMask) != 0u;
    }
}

TEST_CASE("render layer packs into flags bits 18-22 and round-trips")
{
    for (uint32_t layer : {0u, 1u, 5u, 17u, 31u})
    {
        uint32_t flags = packLayer(0u, layer);
        uint32_t decoded = (flags >> LayerShift) & LayerMask;
        CHECK(decoded == layer);
    }

    SUBCASE("out-of-range index aliases via mask, never corrupts other bits")
    {
        uint32_t flags = packLayer(0u, 32u); // 32 & 0x1F == 0
        CHECK(((flags >> LayerShift) & LayerMask) == 0u);
    }
}

TEST_CASE("render layer packing leaves existing flag bits intact")
{
    // Bits used by other ObjectFlags (4-12, 13-17) plus a category in bits 13-16.
    constexpr uint32_t kAlphaMask    = 1u << 4;
    constexpr uint32_t kTranslucent  = 1u << 5;
    constexpr uint32_t kShadowStatic = 1u << 17;
    constexpr uint32_t kCategory     = (0x5u << 13); // some category value in bits 13-16

    uint32_t base = kAlphaMask | kTranslucent | kShadowStatic | kCategory;
    uint32_t flags = packLayer(base, 19u);

    // Original bits survive.
    CHECK((flags & kAlphaMask) != 0u);
    CHECK((flags & kTranslucent) != 0u);
    CHECK((flags & kShadowStatic) != 0u);
    CHECK(((flags >> 13) & 0xFu) == 0x5u);
    // Layer decodes correctly.
    CHECK(((flags >> LayerShift) & LayerMask) == 19u);
}

TEST_CASE("default layer + default mask preserves current behavior (everything visible)")
{
    uint32_t flags = packLayer(0u, 0u);     // default render layer 0
    CHECK(visibleToCamera(flags, 0xFFFFFFFFu)); // default cullingMask = all layers
}

TEST_CASE("culling mask selects layers")
{
    uint32_t layer0 = packLayer(0u, 0u);
    uint32_t layer1 = packLayer(0u, 1u);
    uint32_t layer5 = packLayer(0u, 5u);

    SUBCASE("mask with only bit 1 shows layer 1, hides others")
    {
        uint32_t mask = (1u << 1);
        CHECK_FALSE(visibleToCamera(layer0, mask));
        CHECK(visibleToCamera(layer1, mask));
        CHECK_FALSE(visibleToCamera(layer5, mask));
    }

    SUBCASE("inverted mask hides the excluded layer only")
    {
        uint32_t mask = ~(1u << 5); // everything except layer 5
        CHECK(visibleToCamera(layer0, mask));
        CHECK(visibleToCamera(layer1, mask));
        CHECK_FALSE(visibleToCamera(layer5, mask));
    }

    SUBCASE("zero mask hides everything")
    {
        CHECK_FALSE(visibleToCamera(layer0, 0u));
        CHECK_FALSE(visibleToCamera(layer1, 0u));
    }
}
