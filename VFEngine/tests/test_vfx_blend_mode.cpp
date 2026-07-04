#include <doctest.h>

// VK-1472 — VFX blend modes (Alpha/Additive/Premultiplied/Multiply).
// Covers the pure enum/string/legacy helpers and the loader precedence
// (blendMode string wins over the legacy additiveBlend bool) + JSON round-trip.

#include <render/vfx/billboard/VFXBillboardTypes.hpp>
#include <vfx/VFXAsset.hpp>
#include <vfx/VFXBlendMode.hpp>
#include <vfx/VFXEmitterConfigLoader.hpp>
#include <vfx/VFXEmitterSections.hpp>
#include <vfx/VFXTypes.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    void setBool(vfx::VFXNode& node, const std::string& name, bool value)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Bool, value, 0.0f, 1.0f};
    }

    void setString(vfx::VFXNode& node, const std::string& name, const std::string& value)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::String, value, 0.0f, 0.0f};
    }

    // The loader returns an enum; compare via the (tested) GPU-value contract so a
    // failure prints an integer rather than depending on enum stringification.
    uint32_t gpu(vfx::VFXBlendMode m) { return vfx::blendModeToGpuValue(m); }

    fs::path blendTestRoot()
    {
        return fs::temp_directory_path() / "vf_vfx_blend_mode_tests";
    }

    void resetBlendTestRoot()
    {
        std::error_code ec;
        fs::remove_all(blendTestRoot(), ec);
        fs::create_directories(blendTestRoot(), ec);
    }

    std::string readFileText(const fs::path& path)
    {
        std::ifstream in(path);
        REQUIRE(in.is_open());
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    bool hasSection(const std::vector<std::string>& sections, const char* id)
    {
        return std::find(sections.begin(), sections.end(), id) != sections.end();
    }
}

TEST_SUITE("VFXBlendMode")
{
    TEST_CASE("enum <-> string round-trips; unknown falls back to Alpha")
    {
        CHECK(std::string(vfx::blendModeToString(vfx::VFXBlendMode::Alpha)) == "alpha");
        CHECK(std::string(vfx::blendModeToString(vfx::VFXBlendMode::Additive)) == "additive");
        CHECK(std::string(vfx::blendModeToString(vfx::VFXBlendMode::Premultiplied)) == "premultiplied");
        CHECK(std::string(vfx::blendModeToString(vfx::VFXBlendMode::Multiply)) == "multiply");

        CHECK(gpu(vfx::stringToBlendMode("alpha")) == gpu(vfx::VFXBlendMode::Alpha));
        CHECK(gpu(vfx::stringToBlendMode("additive")) == gpu(vfx::VFXBlendMode::Additive));
        CHECK(gpu(vfx::stringToBlendMode("premultiplied")) == gpu(vfx::VFXBlendMode::Premultiplied));
        CHECK(gpu(vfx::stringToBlendMode("multiply")) == gpu(vfx::VFXBlendMode::Multiply));

        // unknown / empty -> Alpha (safe default)
        CHECK(gpu(vfx::stringToBlendMode("")) == gpu(vfx::VFXBlendMode::Alpha));
        CHECK(gpu(vfx::stringToBlendMode("Additive")) == gpu(vfx::VFXBlendMode::Alpha)); // case-sensitive
        CHECK(gpu(vfx::stringToBlendMode("bogus")) == gpu(vfx::VFXBlendMode::Alpha));

        for (auto m : {vfx::VFXBlendMode::Alpha, vfx::VFXBlendMode::Additive,
                       vfx::VFXBlendMode::Premultiplied, vfx::VFXBlendMode::Multiply})
        {
            CHECK(gpu(vfx::stringToBlendMode(vfx::blendModeToString(m))) == gpu(m));
        }
    }

    TEST_CASE("legacy bool maps to Alpha/Additive")
    {
        CHECK(gpu(vfx::blendModeFromLegacy(true)) == gpu(vfx::VFXBlendMode::Additive));
        CHECK(gpu(vfx::blendModeFromLegacy(false)) == gpu(vfx::VFXBlendMode::Alpha));
    }

    TEST_CASE("GPU push-constant value contract is stable (0..3)")
    {
        // These values are the shader contract; 0/1 must preserve the historical
        // additiveBlend bool meaning. Do not renumber.
        CHECK(vfx::blendModeToGpuValue(vfx::VFXBlendMode::Alpha) == 0u);
        CHECK(vfx::blendModeToGpuValue(vfx::VFXBlendMode::Additive) == 1u);
        CHECK(vfx::blendModeToGpuValue(vfx::VFXBlendMode::Premultiplied) == 2u);
        CHECK(vfx::blendModeToGpuValue(vfx::VFXBlendMode::Multiply) == 3u);

        CHECK(vfx::blendModeNeedsMultiplyPipeline(vfx::VFXBlendMode::Multiply));
        CHECK_FALSE(vfx::blendModeNeedsMultiplyPipeline(vfx::VFXBlendMode::Alpha));
        CHECK_FALSE(vfx::blendModeNeedsMultiplyPipeline(vfx::VFXBlendMode::Additive));
        CHECK_FALSE(vfx::blendModeNeedsMultiplyPipeline(vfx::VFXBlendMode::Premultiplied));
    }

    TEST_CASE("loader: legacy additiveBlend (no blendMode key) maps to the equivalent mode")
    {
        vfx::VFXData data = vfx::VFXAsset::createDefault("blend_legacy");
        vfx::VFXNode* emitter = data.graph.findNode(1);
        REQUIRE(emitter != nullptr);

        emitter->properties.erase("blendMode"); // simulate a pre-VK-1472 asset
        setBool(*emitter, "additiveBlend", true);
        auto config = vfx::VFXEmitterConfigLoader::fromVFXData(data);
        CHECK(gpu(config.blendMode) == gpu(vfx::VFXBlendMode::Additive));

        setBool(*emitter, "additiveBlend", false);
        config = vfx::VFXEmitterConfigLoader::fromVFXData(data);
        CHECK(gpu(config.blendMode) == gpu(vfx::VFXBlendMode::Alpha));
    }

    TEST_CASE("loader: blendMode string WINS over the legacy additiveBlend bool")
    {
        vfx::VFXData data = vfx::VFXAsset::createDefault("blend_precedence");
        vfx::VFXNode* emitter = data.graph.findNode(1);
        REQUIRE(emitter != nullptr);

        setString(*emitter, "blendMode", "multiply");
        setBool(*emitter, "additiveBlend", false);
        auto config = vfx::VFXEmitterConfigLoader::fromVFXData(data);
        CHECK(gpu(config.blendMode) == gpu(vfx::VFXBlendMode::Multiply));

        setString(*emitter, "blendMode", "alpha");
        setBool(*emitter, "additiveBlend", true);
        config = vfx::VFXEmitterConfigLoader::fromVFXData(data);
        CHECK(gpu(config.blendMode) == gpu(vfx::VFXBlendMode::Alpha));

        setString(*emitter, "blendMode", "premultiplied");
        config = vfx::VFXEmitterConfigLoader::fromVFXData(data);
        CHECK(gpu(config.blendMode) == gpu(vfx::VFXBlendMode::Premultiplied));
    }

    TEST_CASE("loader: neither key present defaults to Alpha")
    {
        vfx::VFXData missing;
        vfx::VFXNode emitter;
        emitter.id = 1;
        emitter.type = vfx::VFXNodeType::Emitter;
        emitter.name = "Emitter";
        missing.graph.nodes.push_back(emitter);

        const auto config = vfx::VFXEmitterConfigLoader::fromVFXData(missing);
        CHECK(gpu(config.blendMode) == gpu(vfx::VFXBlendMode::Alpha));
    }

    TEST_CASE(".vfVFX round-trips blendMode; a blendMode-less asset still loads correctly")
    {
        resetBlendTestRoot();

        // New asset with an explicit blendMode survives save/load, version unchanged.
        {
            vfx::VFXData data = vfx::VFXAsset::createDefault("blend_roundtrip");
            vfx::VFXNode* emitter = data.graph.findNode(1);
            REQUIRE(emitter != nullptr);
            setString(*emitter, "blendMode", "premultiplied");

            const fs::path path = blendTestRoot() / "Blend.vfVFX";
            REQUIRE(vfx::VFXAsset::save(path.string(), data));
            CHECK(readFileText(path).find("\"version\": \"1.1\"") != std::string::npos);

            auto loaded = vfx::VFXAsset::load(path.string());
            REQUIRE(loaded.has_value());
            const auto config = vfx::VFXEmitterConfigLoader::fromVFXData(*loaded);
            CHECK(gpu(config.blendMode) == gpu(vfx::VFXBlendMode::Premultiplied));
        }

        // Legacy-style asset on disk (only additiveBlend, no blendMode) maps to Additive.
        {
            vfx::VFXData data = vfx::VFXAsset::createDefault("blend_legacy_disk");
            vfx::VFXNode* emitter = data.graph.findNode(1);
            REQUIRE(emitter != nullptr);
            emitter->properties.erase("blendMode");
            setBool(*emitter, "additiveBlend", true);

            const fs::path path = blendTestRoot() / "BlendLegacy.vfVFX";
            REQUIRE(vfx::VFXAsset::save(path.string(), data));

            auto loaded = vfx::VFXAsset::load(path.string());
            REQUIRE(loaded.has_value());
            const auto config = vfx::VFXEmitterConfigLoader::fromVFXData(*loaded);
            CHECK(gpu(config.blendMode) == gpu(vfx::VFXBlendMode::Additive));
        }
    }

    TEST_CASE("non-default blendMode auto-detects the rendering section")
    {
        vfx::VFXData data = vfx::VFXAsset::createDefault("blend_section");
        const vfx::VFXNode* emitter = data.graph.findEmitterNode();
        REQUIRE(emitter != nullptr);

        vfx::VFXNode node = *emitter;
        setString(node, "blendMode", "multiply");

        vfx::autoDetectEnabledSections(node);
        CHECK(hasSection(node.enabledSections, "rendering"));
    }
}
