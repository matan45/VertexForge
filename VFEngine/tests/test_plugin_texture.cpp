#include "doctest.h"

#include "data/PluginTextureTypes.hpp"
#include "render/custom/PluginTextureManager.hpp"

// VK-1359: plugin texture API + world-space mask — CPU-only contract tests.

TEST_CASE("plugin texture: WorldMaskParams defaults are no-effect")
{
    plugin::WorldMaskParams params;
    CHECK(params.enabled == true);
    CHECK(params.affectsTerrain == false);
    CHECK(params.terrainDimMin == doctest::Approx(0.25f));
    CHECK(params.affectsEntities == false);
    CHECK(params.entityDiscardBelow == doctest::Approx(0.5f));
}

TEST_CASE("plugin texture: handle validity semantics")
{
    plugin::PluginTextureHandle invalid;
    CHECK_FALSE(invalid.isValid());

    plugin::PluginTextureHandle valid{42};
    CHECK(valid.isValid());
}

TEST_CASE("plugin texture: format bytes per pixel")
{
    CHECK(plugin::textureFormatBytesPerPixel(plugin::TextureFormat::R8) == 1u);
    CHECK(plugin::textureFormatBytesPerPixel(plugin::TextureFormat::RGBA8) == 4u);
}

TEST_CASE("plugin texture: world mask flag packing")
{
    using Manager = render::custom::PluginTextureManager;

    plugin::WorldMaskParams params;

    SUBCASE("defaults pack to enabled only")
    {
        CHECK(Manager::packMaskFlags(params, false) == Manager::MASK_FLAG_ENABLED);
    }

    SUBCASE("all effects set all bits")
    {
        params.affectsTerrain = true;
        params.affectsEntities = true;
        const uint32_t flags = Manager::packMaskFlags(params, false);
        CHECK((flags & Manager::MASK_FLAG_ENABLED) != 0u);
        CHECK((flags & Manager::MASK_FLAG_AFFECTS_TERRAIN) != 0u);
        CHECK((flags & Manager::MASK_FLAG_AFFECTS_ENTITIES) != 0u);
    }

    SUBCASE("plugin disable clears the enabled bit but keeps effect bits")
    {
        params.enabled = false;
        params.affectsTerrain = true;
        const uint32_t flags = Manager::packMaskFlags(params, false);
        CHECK((flags & Manager::MASK_FLAG_ENABLED) == 0u);
        CHECK((flags & Manager::MASK_FLAG_AFFECTS_TERRAIN) != 0u);
    }

    SUBCASE("debug force-disable overrides the plugin's enabled flag")
    {
        params.enabled = true;
        params.affectsEntities = true;
        const uint32_t flags = Manager::packMaskFlags(params, true);
        CHECK((flags & Manager::MASK_FLAG_ENABLED) == 0u);
        CHECK((flags & Manager::MASK_FLAG_AFFECTS_ENTITIES) != 0u);
    }
}

TEST_CASE("plugin texture: world mask UBO layout matches the shader std140 block")
{
    using UBO = render::custom::PluginTextureManager::WorldMaskUBOData;
    // vec4 + float + float + uint + float = 32 bytes, std140-compatible
    CHECK(sizeof(UBO) == 32u);
    CHECK(offsetof(UBO, worldMinMax) == 0u);
    CHECK(offsetof(UBO, terrainDimMin) == 16u);
    CHECK(offsetof(UBO, entityDiscardBelow) == 20u);
    CHECK(offsetof(UBO, flags) == 24u);

    UBO data;
    CHECK(data.flags == 0u);                       // unbound = disabled
    CHECK(data.terrainDimMin == doctest::Approx(1.0f));   // no dimming by default
    CHECK(data.entityDiscardBelow == doctest::Approx(0.0f)); // no discard by default
}
