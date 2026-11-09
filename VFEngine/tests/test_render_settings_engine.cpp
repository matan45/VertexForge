#include <doctest.h>

#include <asset/AssetDatabase.hpp>
#include <render/graph/RenderGraphTypes.hpp>
#include <render/postprocess/effects/LutReloadPolicy.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <serialization/SceneSerialization.hpp>
#include <types/RenderSettings.hpp>
#include <types/ShadowPolicy.hpp>

#include <filesystem>
#include <string>
#include <system_error>

// VK-1655 engine fixes the render-settings tools rely on: every tone-mapping mode survives a scene save
// (GranTurismo, AgX and KhronosPBRNeutral used to be written as "aces"), shadow quality Off renders no
// shadows, a LUT change queued before the colour-grading effect reloads is not dropped, and the frame
// graph has the compute-stage read usages the DLSS passes declare.
namespace
{
    namespace fs = std::filesystem;

    struct ScratchDirectory
    {
        fs::path root = fs::temp_directory_path() / "vf_render_settings_engine";

        ScratchDirectory()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
            fs::create_directories(root, ec);
            asset::AssetDatabase::instance().clear();
        }

        ~ScratchDirectory()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
            asset::AssetDatabase::instance().clear();
        }
    };
}

TEST_CASE("render settings engine: every tone-mapping mode survives a scene save and reload")
{
    ScratchDirectory directory;
    for (int index = 0; index <= static_cast<int>(postprocess::ToneMappingMode::KhronosPBRNeutral); ++index)
    {
        const auto mode = static_cast<postprocess::ToneMappingMode>(index);
        CAPTURE(index);

        types::RenderSettings settings = types::RenderSettings::createDefault();
        settings.postProcess.toneMapping.mode = mode;
        scene::SceneGraphSystem source;
        source.setRenderSettings(settings);
        const fs::path scenePath = directory.root / ("ToneMapping" + std::to_string(index) + ".vfScene");
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        asset::AssetDatabase::instance().clear();
        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        CHECK(loaded.getRenderSettings().postProcess.toneMapping.mode == mode);
    }
}

TEST_CASE("render settings engine: shadow quality Off renders no shadows, like enabled false")
{
    types::ShadowSettings shadows;
    for (const bool enabled : {false, true})
    {
        for (const types::ShadowQuality quality : {types::ShadowQuality::Off, types::ShadowQuality::Low,
                                                   types::ShadowQuality::Medium, types::ShadowQuality::High,
                                                   types::ShadowQuality::Ultra})
        {
            shadows.enabled = enabled;
            shadows.quality = quality;
            CAPTURE(enabled);
            CAPTURE(static_cast<int>(quality));
            CHECK(types::shadowsRendered(shadows) == (enabled && quality != types::ShadowQuality::Off));
        }
    }
}

TEST_CASE("render settings engine: a LUT request is compared against the reload already queued")
{
    std::string current = "warm.cube";
    std::string pending;
    bool pendingActive = false;

    SUBCASE("asking for the loaded LUT does nothing")
    {
        render::postprocess::requestLut(current, pending, pendingActive, "warm.cube");
        CHECK_FALSE(pendingActive);
    }

    SUBCASE("a new LUT is queued")
    {
        render::postprocess::requestLut(current, pending, pendingActive, "cold.cube");
        CHECK(pendingActive);
        CHECK(pending == "cold.cube");
    }

    SUBCASE("a second change before the reload replaces the queued one")
    {
        render::postprocess::requestLut(current, pending, pendingActive, "cold.cube");
        render::postprocess::requestLut(current, pending, pendingActive, "noir.cube");
        CHECK(pendingActive);
        CHECK(pending == "noir.cube");
    }

    SUBCASE("going back to the loaded LUT (an edit and its undo in one drain) cancels the reload")
    {
        render::postprocess::requestLut(current, pending, pendingActive, "cold.cube");
        render::postprocess::requestLut(current, pending, pendingActive, "warm.cube");
        CHECK_FALSE(pendingActive);
        CHECK(pending.empty());
    }

    SUBCASE("clearing a loaded LUT is queued like any other path")
    {
        render::postprocess::requestLut(current, pending, pendingActive, "");
        CHECK(pendingActive);
        CHECK(pending.empty());
    }

    CHECK(current == "warm.cube"); // only the effect's reload changes the loaded path
}

TEST_CASE("render settings engine: the DLSS passes' compute-stage read usages")
{
    using render::graph::ResourceUsage;

    // The pre-upscale pass samples the scene colour in fragment shaders (the effects) AND in compute (the
    // reactive mask); DLSS and the motion-vector fallback read depth in compute.
    const render::graph::UsageMapping anyShader = render::graph::getUsageMapping(ResourceUsage::SampledReadAnyShader);
    CHECK(anyShader.stage == (vk::PipelineStageFlags2(vk::PipelineStageFlagBits2::eFragmentShader) |
                              vk::PipelineStageFlagBits2::eComputeShader));
    CHECK(anyShader.access == vk::AccessFlags2(vk::AccessFlagBits2::eShaderSampledRead));
    CHECK(anyShader.layout == vk::ImageLayout::eShaderReadOnlyOptimal);

    const render::graph::UsageMapping depth = render::graph::getUsageMapping(ResourceUsage::DepthSampledComputeRead);
    CHECK(depth.stage == vk::PipelineStageFlags2(vk::PipelineStageFlagBits2::eComputeShader));
    CHECK(depth.access == vk::AccessFlags2(vk::AccessFlagBits2::eShaderSampledRead));
    CHECK(depth.layout == vk::ImageLayout::eDepthStencilReadOnlyOptimal);
}
