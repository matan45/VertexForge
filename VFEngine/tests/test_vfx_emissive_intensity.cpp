#include <doctest.h>

#include <render/VFXGlowMath.hpp>
#include <render/vfx/billboard/VFXBillboardTypes.hpp>
#include <vfx/VFXAsset.hpp>
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

    void setFloat(vfx::VFXNode& node, const std::string& name, float value, float min = 0.0f, float max = 100.0f)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Float, value, min, max};
    }

    fs::path emissiveTestRoot()
    {
        return fs::temp_directory_path() / "vf_vfx_emissive_intensity_tests";
    }

    void resetEmissiveTestRoot()
    {
        std::error_code ec;
        fs::remove_all(emissiveTestRoot(), ec);
        fs::create_directories(emissiveTestRoot(), ec);
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

TEST_SUITE("VFXEmissiveIntensity")
{
    TEST_CASE("emissive glow scale is multiplicative")
    {
        CHECK(render::computeEmissiveGlowScale(0.75f, 1.0f) == doctest::Approx(0.75f));
        CHECK(render::computeEmissiveGlowScale(0.75f, 0.0f) == doctest::Approx(0.0f));
        CHECK(render::computeEmissiveGlowScale(0.5f, 4.0f) == doctest::Approx(2.0f));
        CHECK(render::computeEmissiveGlowScale(0.5f, 4.0f) >
              render::computeEmissiveGlowScale(0.5f, 2.0f));
    }

    TEST_CASE("loader reads emissive intensity clamps negatives and defaults to neutral")
    {
        vfx::VFXData data = vfx::VFXAsset::createDefault("emissive_loader");
        vfx::VFXNode* emitter = data.graph.findNode(1);
        REQUIRE(emitter != nullptr);

        setFloat(*emitter, "emissiveIntensity", 3.5f);
        render::vfx::VFXEmitterConfig config = vfx::VFXEmitterConfigLoader::fromVFXData(data);
        CHECK(config.emissiveIntensity == doctest::Approx(3.5f));

        setFloat(*emitter, "emissiveIntensity", -2.0f);
        config = vfx::VFXEmitterConfigLoader::fromVFXData(data);
        CHECK(config.emissiveIntensity == doctest::Approx(0.0f));

        vfx::VFXData missing;
        vfx::VFXNode missingEmitter;
        missingEmitter.id = 1;
        missingEmitter.type = vfx::VFXNodeType::Emitter;
        missingEmitter.name = "Emitter";
        missing.graph.nodes.push_back(missingEmitter);

        const render::vfx::VFXEmitterConfig defaults = vfx::VFXEmitterConfigLoader::fromVFXData(missing);
        CHECK(defaults.emissiveIntensity == doctest::Approx(vfx::EmitterDefaults::EMISSIVE_INTENSITY));
    }

    TEST_CASE(".vfVFX round-trips emissive intensity without changing format version")
    {
        resetEmissiveTestRoot();

        vfx::VFXData data = vfx::VFXAsset::createDefault("emissive_roundtrip");
        vfx::VFXNode* emitter = data.graph.findNode(1);
        REQUIRE(emitter != nullptr);
        setFloat(*emitter, "emissiveIntensity", 6.25f);

        const fs::path path = emissiveTestRoot() / "Emissive.vfVFX";
        REQUIRE(vfx::VFXAsset::save(path.string(), data));
        CHECK(readFileText(path).find("\"version\": \"1.1\"") != std::string::npos);

        auto loaded = vfx::VFXAsset::load(path.string());
        REQUIRE(loaded.has_value());
        const render::vfx::VFXEmitterConfig config = vfx::VFXEmitterConfigLoader::fromVFXData(*loaded);
        CHECK(config.emissiveIntensity == doctest::Approx(6.25f));
    }

    TEST_CASE("non-default emissive intensity auto-detects lighting section")
    {
        vfx::VFXData data = vfx::VFXAsset::createDefault("emissive_section");
        const vfx::VFXNode* emitter = data.graph.findEmitterNode();
        REQUIRE(emitter != nullptr);

        vfx::VFXNode node = *emitter;
        node.properties["emissiveIntensity"].value = 2.0f;

        vfx::autoDetectEnabledSections(node);
        CHECK(hasSection(node.enabledSections, "lighting"));
    }
}
