#include <doctest.h>

#include "test_repo_scan_helpers.hpp"

#include <asset/AssetDatabase.hpp>
#include <export/ShaderCompiler.hpp>
#include <render/postprocess/effects/FXAAEffect.hpp>
#include <resource/ShaderResource.hpp>
#include <resource/Types.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <serialization/SceneSerialization.hpp>
#include <types/RenderSettings.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

// VK-1661: FXAA 3.11 Quality as a post-process effect. These cases pin what no validation layer reports:
// fxaa.glsl compiles (a broken shader just draws nothing), samples only with explicit LOD (its edge search
// runs in divergent control flow) and reads the push constants FXAAEffect writes, field for field; and
// postProcess.fxaa survives a scene save + reload, with the loader clamping to FXAA 3.11's tuning limits.
namespace
{
    namespace fs = std::filesystem;

    // Tests.exe runs from bin/Tests/<Config>/x64/ and resources/ is not copied there (see
    // test_text_shader_compile.cpp).
    fs::path shaderRoot()
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        return *root / "resources" / "shaders";
    }

    std::string readText(const fs::path& path)
    {
        std::ifstream in(path);
        REQUIRE_MESSAGE(in.is_open(), "missing " << path.string());
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // Compiles every #type stage, with includes resolved relative to the shader's own folder (the same base
    // path the engine and the exporter use).
    void requireAllStagesCompile(const fs::path& shaderPath)
    {
        REQUIRE_MESSAGE(fs::exists(shaderPath), "missing " << shaderPath.string());

        const auto stages = resource::ShaderResource::readShaderFile(shaderPath.string());
        REQUIRE_MESSAGE(!stages.empty(), "no #type stages parsed from " << shaderPath.string());

        shaderCompiler::CompileOptions options;
        options.includeBasePath = shaderPath.parent_path();

        bool sawVertex = false;
        bool sawFragment = false;
        for (const auto& stage : stages)
        {
            sawVertex = sawVertex || stage.type == resource::ShaderType::VERTEX;
            sawFragment = sawFragment || stage.type == resource::ShaderType::FRAGMENT;

            const auto vkStage = shaderCompiler::shaderTypeToVulkanStage(static_cast<uint8_t>(stage.type));
            const std::string name = shaderPath.filename().string() + ":" + std::to_string(static_cast<int>(stage.type));

            // compile() returns an empty vector and logs on failure.
            const auto spirv = shaderCompiler::compile(stage.source, vkStage, name, options);
            CHECK_MESSAGE(!spirv.empty(), "failed to compile " << name);
        }

        CHECK(sawVertex);
        CHECK(sawFragment);
    }

    struct ScratchDirectory
    {
        fs::path root = fs::temp_directory_path() / "vf_postprocess_fxaa";

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

    postprocess::FXAASettings loadFxaa(const fs::path& scenePath)
    {
        asset::AssetDatabase::instance().clear();
        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        return loaded.getRenderSettings().postProcess.fxaa;
    }

    fs::path writeScene(const ScratchDirectory& directory, const types::RenderSettings& settings, const std::string& name)
    {
        scene::SceneGraphSystem source;
        source.setRenderSettings(settings);
        const fs::path scenePath = directory.root / (name + ".vfScene");
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));
        return scenePath;
    }
}

TEST_CASE("fxaa: fxaa.glsl compiles both stages")
{
    requireAllStagesCompile(shaderRoot() / "postprocess" / "fxaa.glsl");
}

TEST_CASE("fxaa: the shader samples with explicit LOD and reads the push constants FXAAEffect writes")
{
    const std::string source = readText(shaderRoot() / "postprocess" / "fxaa.glsl");

    // The end-of-edge search branches per pixel, where implicit derivatives (texture()) are undefined.
    CHECK(source.find("texture(") == std::string::npos);
    CHECK(source.find("textureLod(") != std::string::npos);
    CHECK(source.find("textureLodOffset(") != std::string::npos);
    // The texel size comes from the bound image, never from a stored extent that can go stale.
    CHECK(source.find("1.0 / vec2(textureSize(inputTexture, 0))") != std::string::npos);
    // FXAA_QUALITY__PS of presets 10 / 12 / 29 / 39; the guards against 1.0 / range on a flat block and
    // against a quality index past Ultra (a plugin can apply unchecked settings).
    CHECK(source.find("int[4](3, 5, 12, 12)") != std::string::npos);
    CHECK(source.find("!(range > 0.0)") != std::string::npos);
    CHECK(source.find("min(pc.quality, 3u)") != std::string::npos);

    // The push_constant block lists FXAAPushConstants' fields in its order.
    const std::size_t subpixel = source.find("float subpixel;");
    const std::size_t edgeThreshold = source.find("float edgeThreshold;");
    const std::size_t edgeThresholdMin = source.find("float edgeThresholdMin;");
    const std::size_t quality = source.find("uint quality;");
    REQUIRE(subpixel != std::string::npos);
    REQUIRE(edgeThreshold != std::string::npos);
    REQUIRE(edgeThresholdMin != std::string::npos);
    REQUIRE(quality != std::string::npos);
    CHECK(subpixel < edgeThreshold);
    CHECK(edgeThreshold < edgeThresholdMin);
    CHECK(edgeThresholdMin < quality);

    using render::postprocess::FXAAPushConstants;
    CHECK(sizeof(FXAAPushConstants) == 16);
    CHECK(offsetof(FXAAPushConstants, subpixel) == 0);
    CHECK(offsetof(FXAAPushConstants, edgeThreshold) == 4);
    CHECK(offsetof(FXAAPushConstants, edgeThresholdMin) == 8);
    CHECK(offsetof(FXAAPushConstants, quality) == 12);

    const FXAAPushConstants defaults{};
    const postprocess::FXAASettings settings{};
    CHECK(defaults.subpixel == settings.subpixel);
    CHECK(defaults.edgeThreshold == settings.edgeThreshold);
    CHECK(defaults.edgeThresholdMin == settings.edgeThresholdMin);
    CHECK(defaults.quality == static_cast<uint32_t>(settings.quality));
}

TEST_CASE("fxaa: every quality and the tuning values survive a scene save and reload")
{
    ScratchDirectory directory;
    for (int index = 0; index <= static_cast<int>(postprocess::FXAAQuality::Ultra); ++index)
    {
        CAPTURE(index);
        types::RenderSettings settings = types::RenderSettings::createDefault();
        settings.postProcess.fxaa.enabled = true;
        settings.postProcess.fxaa.quality = static_cast<postprocess::FXAAQuality>(index);
        settings.postProcess.fxaa.subpixel = 0.5f;
        settings.postProcess.fxaa.edgeThreshold = 0.2f;
        settings.postProcess.fxaa.edgeThresholdMin = 0.05f;

        const postprocess::FXAASettings reloaded =
            loadFxaa(writeScene(directory, settings, "Fxaa" + std::to_string(index)));
        CHECK(reloaded.enabled);
        CHECK(reloaded.quality == settings.postProcess.fxaa.quality);
        CHECK(reloaded.subpixel == 0.5f);
        CHECK(reloaded.edgeThreshold == 0.2f);
        CHECK(reloaded.edgeThresholdMin == 0.05f);
    }
}

TEST_CASE("fxaa: the scene loader clamps to FXAA 3.11's limits and reads an unknown quality as Medium")
{
    ScratchDirectory directory;
    const fs::path scenePath = writeScene(directory, types::RenderSettings::createDefault(), "FxaaClamp");

    // The (de)serializers are private: edit the written .vfSettings the way a hand edit would.
    const fs::path settingsPath = directory.root / "FxaaClamp.vfSettings";
    nlohmann::json file = nlohmann::json::parse(readText(settingsPath));
    REQUIRE(file.contains("renderSettings"));
    REQUIRE(file["renderSettings"].contains("postProcess"));
    REQUIRE(file["renderSettings"]["postProcess"].contains("fxaa"));
    file["renderSettings"]["postProcess"]["fxaa"] = {
        {"enabled", true}, {"quality", "bogus"}, {"subpixel", 5.0}, {"edgeThreshold", 0.0}, {"edgeThresholdMin", 1.0}};
    {
        std::ofstream out(settingsPath, std::ios::trunc);
        REQUIRE(out.is_open());
        out << file.dump(2);
    }

    const postprocess::FXAASettings fxaa = loadFxaa(scenePath);
    CHECK(fxaa.enabled);
    CHECK(fxaa.quality == postprocess::FXAAQuality::Medium);
    CHECK(fxaa.subpixel == 1.0f);
    CHECK(fxaa.edgeThreshold == 0.063f);
    CHECK(fxaa.edgeThresholdMin == 0.0833f);
}
