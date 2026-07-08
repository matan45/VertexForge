#include <doctest.h>

#include <vfx/VFXBundleSignature.hpp>

#include <string>

// ============================================================
// VK-1483: VFXBundleSignature is the pure, Vulkan-free reuse-decision seam for the
// composited VFXSequence preview. A parked StepBundle (pipeline + shared mesh cache)
// may be reused across a seek/loop only when its signature matches the requested
// step's — otherwise the preview must rebuild the heavy GPU resources. These tests
// exercise that decision directly; the bundles themselves live in the graphics module
// and are GPU-only, so this is the CPU-testable surface.
// ============================================================

namespace
{
    using namespace vfx;

    VFXBundleSignature sig(int renderMode, std::string meshPath = {}, std::string texturePath = {})
    {
        VFXBundleSignature s;
        s.renderMode = renderMode;
        s.meshPath = std::move(meshPath);
        s.texturePath = std::move(texturePath);
        return s;
    }
}

TEST_CASE("VFXBundleSignature: identical signatures are reusable")
{
    SUBCASE("mesh step with same mesh + texture")
    {
        const auto a = sig(3, "assets/vfx/ice_shard.vfMesh", "assets/vfx/ice.png");
        const auto b = sig(3, "assets/vfx/ice_shard.vfMesh", "assets/vfx/ice.png");
        CHECK(a == b);
        CHECK(canReuseBundle(a, b));
    }

    SUBCASE("billboard step with no mesh, same texture")
    {
        const auto a = sig(0, "", "assets/vfx/spark.png");
        const auto b = sig(0, "", "assets/vfx/spark.png");
        CHECK(canReuseBundle(a, b));
    }

    SUBCASE("empty-vs-empty (default-constructed) reuses")
    {
        CHECK(canReuseBundle(VFXBundleSignature{}, VFXBundleSignature{}));
    }
}

TEST_CASE("VFXBundleSignature: any differing field forbids reuse")
{
    const auto base = sig(3, "assets/vfx/ice_shard.vfMesh", "assets/vfx/ice.png");

    SUBCASE("different render mode")
    {
        const auto other = sig(4, "assets/vfx/ice_shard.vfMesh", "assets/vfx/ice.png");
        CHECK_FALSE(canReuseBundle(base, other));
    }

    SUBCASE("different mesh path")
    {
        const auto other = sig(3, "assets/vfx/rock_shard.vfMesh", "assets/vfx/ice.png");
        CHECK_FALSE(canReuseBundle(base, other));
    }

    SUBCASE("different texture path")
    {
        const auto other = sig(3, "assets/vfx/ice_shard.vfMesh", "assets/vfx/fire.png");
        CHECK_FALSE(canReuseBundle(base, other));
    }

    SUBCASE("mesh path added where there was none")
    {
        const auto none = sig(3, "", "assets/vfx/ice.png");
        CHECK_FALSE(canReuseBundle(none, base));
    }
}
