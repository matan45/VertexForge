#include "doctest.h"

#include "data/PostProcessEffectTypes.hpp"
#include <cstddef>

// VK-1409 — CPU-only contract tests for the plugin custom post-process effect API.
// The GLSL/pipeline/ping-pong path needs a GPU device (not available in Tests), so
// these guard the pure surface: the ABI desc, the priority ordering relative to the
// built-in tonemap, and the validation gate the engine actually uses.

TEST_CASE("postprocess effect: desc defaults")
{
    plugin::PostProcessEffectDesc desc;
    CHECK(desc.order == plugin::PostProcessOrder::AfterTonemap);
    CHECK(desc.customPriority == 0u);
    CHECK(desc.paramsSize == 0u);
    CHECK(desc.startEnabled == true);
    CHECK(desc.fragmentGlsl.empty());
}

TEST_CASE("postprocess effect: params budget fits the Vulkan 128-byte minimum")
{
    CHECK(plugin::MAX_POSTPROCESS_PARAMS_SIZE == 64u);
    CHECK(plugin::MAX_POSTPROCESS_PARAMS_SIZE <= 128u);
}

TEST_CASE("postprocess effect: order maps to priority straddling the built-in tonemap (100)")
{
    // Built-in tone mapping is priority 100 (ToneMappingEffect::getPriority()).
    constexpr uint32_t kBuiltinTonemapPriority = 100u;

    plugin::PostProcessEffectDesc before;
    before.order = plugin::PostProcessOrder::BeforeTonemap;
    CHECK(plugin::postProcessOrderToPriority(before) < kBuiltinTonemapPriority);

    plugin::PostProcessEffectDesc after;
    after.order = plugin::PostProcessOrder::AfterTonemap;
    CHECK(plugin::postProcessOrderToPriority(after) > kBuiltinTonemapPriority);

    CHECK(plugin::postProcessOrderToPriority(before) == plugin::POSTPROCESS_PRIORITY_BEFORE_TONEMAP);
    CHECK(plugin::postProcessOrderToPriority(after) == plugin::POSTPROCESS_PRIORITY_AFTER_TONEMAP);
}

TEST_CASE("postprocess effect: customPriority overrides the order enum")
{
    plugin::PostProcessEffectDesc desc;
    desc.order = plugin::PostProcessOrder::AfterTonemap; // would be 130
    desc.customPriority = 42u;
    CHECK(plugin::postProcessOrderToPriority(desc) == 42u);

    // customPriority == 0 means "derive from order" (0 is not a usable priority here).
    desc.customPriority = 0u;
    CHECK(plugin::postProcessOrderToPriority(desc) == plugin::POSTPROCESS_PRIORITY_AFTER_TONEMAP);
}

TEST_CASE("postprocess effect: validation rejects malformed descriptors")
{
    plugin::PostProcessEffectDesc good;
    good.fragmentGlsl = "#version 460 core\nvoid main(){}";
    good.paramsSize = 16u;
    CHECK(plugin::validatePostProcessEffectDesc(good));

    SUBCASE("empty GLSL rejected")
    {
        plugin::PostProcessEffectDesc d = good;
        d.fragmentGlsl.clear();
        CHECK_FALSE(plugin::validatePostProcessEffectDesc(d));
    }

    SUBCASE("oversized params rejected")
    {
        plugin::PostProcessEffectDesc d = good;
        d.paramsSize = plugin::MAX_POSTPROCESS_PARAMS_SIZE + 1u;
        CHECK_FALSE(plugin::validatePostProcessEffectDesc(d));
    }

    SUBCASE("exactly-max params accepted")
    {
        plugin::PostProcessEffectDesc d = good;
        d.paramsSize = plugin::MAX_POSTPROCESS_PARAMS_SIZE;
        CHECK(plugin::validatePostProcessEffectDesc(d));
    }

    SUBCASE("zero params accepted (no push constants)")
    {
        plugin::PostProcessEffectDesc d = good;
        d.paramsSize = 0u;
        CHECK(plugin::validatePostProcessEffectDesc(d));
    }
}

TEST_CASE("postprocess effect: handle validity")
{
    plugin::PostProcessEffectHandle invalid;
    CHECK_FALSE(invalid.isValid());

    plugin::PostProcessEffectHandle valid{7};
    CHECK(valid.isValid());
}
