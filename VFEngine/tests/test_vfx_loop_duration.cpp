#include <doctest.h>

#include <render/vfx/billboard/VFXBillboardTypes.hpp>
#include <vfx/VFXAsset.hpp>
#include <vfx/VFXEmitterConfigLoader.hpp>
#include <vfx/VFXTypes.hpp>

#include <filesystem>
#include <limits>

namespace
{
    namespace fs = std::filesystem;

    fs::path loopDurationTestRoot()
    {
        return fs::temp_directory_path() / "vf_vfx_loop_duration_tests";
    }

    void resetLoopDurationTestRoot()
    {
        std::error_code ec;
        fs::remove_all(loopDurationTestRoot(), ec);
        fs::create_directories(loopDurationTestRoot(), ec);
    }

    void setLoopDuration(vfx::VFXNode& node, float value)
    {
        node.properties["loopDuration"] = vfx::VFXProperty{
            "loopDuration", vfx::VFXPropertyType::Float, value, 0.0f, 60.0f};
    }
}

TEST_SUITE("VFXLoopDuration")
{
    TEST_CASE("new emitters expose an automatic loop duration by default")
    {
        const vfx::VFXData data = vfx::VFXAsset::createDefault("loop_default");
        const vfx::VFXNode* emitter = data.graph.findEmitterNode();
        REQUIRE(emitter != nullptr);

        const auto it = emitter->properties.find("loopDuration");
        REQUIRE(it != emitter->properties.end());
        CHECK(it->second.type == vfx::VFXPropertyType::Float);
        const float* value = std::get_if<float>(&it->second.value);
        REQUIRE(value != nullptr);
        CHECK(*value == doctest::Approx(vfx::EmitterDefaults::LOOP_DURATION));
        CHECK(it->second.min == doctest::Approx(0.0f));
        CHECK(it->second.max == doctest::Approx(60.0f));
    }

    TEST_CASE("loader preserves explicit duration and normalizes invalid values")
    {
        vfx::VFXData data = vfx::VFXAsset::createDefault("loop_loader");
        vfx::VFXNode* emitter = data.graph.findNode(1);
        REQUIRE(emitter != nullptr);

        setLoopDuration(*emitter, 4.15f);
        CHECK(vfx::VFXEmitterConfigLoader::fromVFXData(data).loopDuration ==
              doctest::Approx(4.15f));

        setLoopDuration(*emitter, -1.0f);
        CHECK(vfx::VFXEmitterConfigLoader::fromVFXData(data).loopDuration == 0.0f);

        setLoopDuration(*emitter, std::numeric_limits<float>::quiet_NaN());
        CHECK(vfx::VFXEmitterConfigLoader::fromVFXData(data).loopDuration == 0.0f);
    }

    TEST_CASE("legacy assets gain the property and explicit values round-trip without a format bump")
    {
        resetLoopDurationTestRoot();

        vfx::VFXData legacy = vfx::VFXAsset::createDefault("loop_legacy");
        vfx::VFXNode* legacyEmitter = legacy.graph.findNode(1);
        REQUIRE(legacyEmitter != nullptr);
        legacyEmitter->properties.erase("loopDuration");

        const fs::path legacyPath = loopDurationTestRoot() / "Legacy.vfVFX";
        REQUIRE(vfx::VFXAsset::save(legacyPath.string(), legacy));
        auto loadedLegacy = vfx::VFXAsset::load(legacyPath.string());
        REQUIRE(loadedLegacy.has_value());
        CHECK(loadedLegacy->version == "1.2");
        const vfx::VFXNode* migratedEmitter = loadedLegacy->graph.findEmitterNode();
        REQUIRE(migratedEmitter != nullptr);
        CHECK(migratedEmitter->properties.count("loopDuration") == 1);
        CHECK(vfx::VFXEmitterConfigLoader::fromVFXData(*loadedLegacy).loopDuration == 0.0f);

        vfx::VFXData explicitData = vfx::VFXAsset::createDefault("loop_explicit");
        vfx::VFXNode* explicitEmitter = explicitData.graph.findNode(1);
        REQUIRE(explicitEmitter != nullptr);
        setLoopDuration(*explicitEmitter, 6.25f);

        const fs::path explicitPath = loopDurationTestRoot() / "Explicit.vfVFX";
        REQUIRE(vfx::VFXAsset::save(explicitPath.string(), explicitData));
        auto loadedExplicit = vfx::VFXAsset::load(explicitPath.string());
        REQUIRE(loadedExplicit.has_value());
        CHECK(loadedExplicit->version == "1.2");
        CHECK(vfx::VFXEmitterConfigLoader::fromVFXData(*loadedExplicit).loopDuration ==
              doctest::Approx(6.25f));
    }
}
