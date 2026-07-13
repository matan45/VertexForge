// CPU-only codec coverage for the VK-1453 (Phase 4) additive asset fields:
//
//   1. .vfVFX round-trip: a VFXData carrying Fixed bounds, an enabled per-tier
//      scalability profile and cullEligible=true must save and reload field-by-
//      field identical, and the saved file must stamp the current version "1.2".
//   2. Backward compatibility: a hand-written 1.0-style .vfVFX with none of the
//      Phase-4 keys loads with the neutral struct defaults (Auto bounds,
//      scalability disabled, cull off).
//   3. .vfVFXSequence round-trip: aggregate bounds survive save/load and the
//      saved file stamps the current sequence format version.
//
// The seam is the public VFXAsset / VFXSequenceAsset save & load over temp files.
// No graphics layer, no Vulkan device.

#include <doctest.h>

#include <vfx/VFXAsset.hpp>
#include <vfx/VFXSequenceAsset.hpp>
#include <vfx/VFXScalability.hpp>
#include <vfx/VFXSequenceTypes.hpp>
#include <vfx/VFXTypes.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path boundsTestRoot()
    {
        return fs::temp_directory_path() / "vf_vfx_bounds_serialization_tests";
    }

    void resetBoundsTestRoot()
    {
        std::error_code ec;
        fs::remove_all(boundsTestRoot(), ec);
        fs::create_directories(boundsTestRoot(), ec);
    }

    std::string readVersionField(const fs::path& path)
    {
        std::ifstream in(path);
        REQUIRE(in.is_open());
        json j = json::parse(in);
        return j.value("version", std::string{});
    }

    vfx::VFXData makeBoundsVFX()
    {
        vfx::VFXData data;
        data.version = vfx::VFX_FORMAT_VERSION;
        data.uuid = "bounds-uuid-123";
        data.name = "BoundsFX";

        // Minimal but valid-looking graph so the round-trip exercises the graph
        // block alongside the new fields.
        vfx::VFXNode emitter;
        emitter.id = 1;
        emitter.type = vfx::VFXNodeType::Emitter;
        emitter.name = "Emitter";
        data.graph.nodes.push_back(emitter);
        data.graph.nextNodeId = 2;

        // Fixed, non-symmetric bounds (distinct center + extents).
        data.bounds.mode = vfx::VFXBoundsMode::Fixed;
        data.bounds.center = glm::vec3(1.0f, 2.0f, 3.0f);
        data.bounds.extents = glm::vec3(4.0f, 5.0f, 6.0f);

        // Enabled scalability with a distinct value in every field of every tier.
        data.scalability.enabled = true;
        for (int i = 0; i < vfx::kVFXQualityTierCount; ++i)
        {
            vfx::VFXScalabilityLevel& level = data.scalability.levels[i];
            level.spawnRateScale = 0.25f * static_cast<float>(i + 1);
            level.maxParticles = 100 * (i + 1);
            level.cullDistance = 50.0f * static_cast<float>(i + 1);
            level.updateInterval = i + 1;
            level.rendererEnabled = (i != 0); // tier 0 disabled, rest enabled
        }

        data.cullEligible = true;
        return data;
    }
}

TEST_SUITE("VFXBoundsSerialization")
{
    TEST_CASE(".vfVFX round-trips bounds, scalability and cullEligible; stamps 1.2")
    {
        resetBoundsTestRoot();

        const vfx::VFXData original = makeBoundsVFX();
        const fs::path path = boundsTestRoot() / "BoundsFX.vfVFX";

        REQUIRE(vfx::VFXAsset::save(path.string(), original));
        CHECK(readVersionField(path) == "1.2");

        auto loadedOpt = vfx::VFXAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        const vfx::VFXData& loaded = *loadedOpt;

        CHECK(loaded.uuid == original.uuid);
        CHECK(loaded.name == original.name);

        // Bounds.
        CHECK(loaded.bounds.mode == vfx::VFXBoundsMode::Fixed);
        CHECK(loaded.bounds.center.x == doctest::Approx(1.0f));
        CHECK(loaded.bounds.center.y == doctest::Approx(2.0f));
        CHECK(loaded.bounds.center.z == doctest::Approx(3.0f));
        CHECK(loaded.bounds.extents.x == doctest::Approx(4.0f));
        CHECK(loaded.bounds.extents.y == doctest::Approx(5.0f));
        CHECK(loaded.bounds.extents.z == doctest::Approx(6.0f));

        // Scalability.
        CHECK(loaded.scalability.enabled == true);
        for (int i = 0; i < vfx::kVFXQualityTierCount; ++i)
        {
            const vfx::VFXScalabilityLevel& a = loaded.scalability.levels[i];
            const vfx::VFXScalabilityLevel& b = original.scalability.levels[i];
            CHECK(a.spawnRateScale == doctest::Approx(b.spawnRateScale));
            CHECK(a.maxParticles == b.maxParticles);
            CHECK(a.cullDistance == doctest::Approx(b.cullDistance));
            CHECK(a.updateInterval == b.updateInterval);
            CHECK(a.rendererEnabled == b.rendererEnabled);
        }

        // Cull opt-in.
        CHECK(loaded.cullEligible == true);
    }

    TEST_CASE("a 1.0 .vfVFX without Phase-4 keys loads with neutral defaults")
    {
        resetBoundsTestRoot();

        // Legacy 1.0 file: only version/uuid/name/graph, no bounds/scalability/cull.
        json j;
        j["version"] = "1.0";
        j["uuid"] = "legacy-vfx";
        j["name"] = "LegacyFX";

        json emitter;
        emitter["id"] = 1;
        emitter["type"] = "Emitter";
        emitter["name"] = "Emitter";
        emitter["position"] = json::array({0.0f, 0.0f});
        emitter["properties"] = json::object();

        json graph;
        graph["nodes"] = json::array({emitter});
        graph["links"] = json::array();
        j["graph"] = graph;

        const fs::path path = boundsTestRoot() / "LegacyFX.vfVFX";
        {
            std::ofstream file(path);
            REQUIRE(file.is_open());
            file << j.dump(4);
        }

        auto loadedOpt = vfx::VFXAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        const vfx::VFXData& loaded = *loadedOpt;

        CHECK(loaded.bounds.mode == vfx::VFXBoundsMode::Auto);
        CHECK(loaded.bounds.center.x == doctest::Approx(0.0f));
        CHECK(loaded.bounds.extents.x == doctest::Approx(0.0f));

        CHECK(loaded.scalability.enabled == false);
        // Neutral level defaults preserved.
        CHECK(loaded.scalability.levels[0].spawnRateScale == doctest::Approx(1.0f));
        CHECK(loaded.scalability.levels[0].maxParticles == -1);
        CHECK(loaded.scalability.levels[0].updateInterval == 1);
        CHECK(loaded.scalability.levels[0].rendererEnabled == true);

        CHECK(loaded.cullEligible == false);
    }

    TEST_CASE("a freshly-defaulted VFXData round-trips as neutral Phase-4 fields")
    {
        resetBoundsTestRoot();

        vfx::VFXData original; // all defaults
        original.uuid = "default-vfx";
        original.name = "DefaultFX";

        const fs::path path = boundsTestRoot() / "DefaultFX.vfVFX";
        REQUIRE(vfx::VFXAsset::save(path.string(), original));

        auto loadedOpt = vfx::VFXAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        const vfx::VFXData& loaded = *loadedOpt;

        CHECK(loaded.bounds.mode == vfx::VFXBoundsMode::Auto);
        CHECK(loaded.scalability.enabled == false);
        CHECK(loaded.cullEligible == false);
    }

    TEST_CASE(".vfVFXSequence round-trips aggregate bounds + stableLoop; stamps 1.5")
    {
        resetBoundsTestRoot();

        vfx::VFXSequenceData original;
        original.uuid = "bounds-seq";
        original.name = "BoundsSequence";
        original.bounds.mode = vfx::VFXBoundsMode::Fixed;
        original.bounds.center = glm::vec3(-1.5f, 0.5f, 7.0f);
        original.bounds.extents = glm::vec3(2.0f, 3.0f, 4.0f);
        original.stableLoop = true; // VK-1498 — additive flag, round-trips with the asset
        // steps intentionally left empty — bounds are independent of steps here.

        const fs::path path = boundsTestRoot() / "BoundsSequence.vfVFXSequence";
        REQUIRE(vfx::VFXSequenceAsset::save(original, path.string()));
        CHECK(readVersionField(path) == "1.5"); // VK-1498 bumped the sequence format version

        auto loadedOpt = vfx::VFXSequenceAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        const vfx::VFXSequenceData& loaded = *loadedOpt;

        CHECK(loaded.bounds.mode == vfx::VFXBoundsMode::Fixed);
        CHECK(loaded.bounds.center.x == doctest::Approx(-1.5f));
        CHECK(loaded.bounds.center.y == doctest::Approx(0.5f));
        CHECK(loaded.bounds.center.z == doctest::Approx(7.0f));
        CHECK(loaded.bounds.extents.x == doctest::Approx(2.0f));
        CHECK(loaded.bounds.extents.y == doctest::Approx(3.0f));
        CHECK(loaded.bounds.extents.z == doctest::Approx(4.0f));
        CHECK(loaded.stableLoop == true);
    }

    TEST_CASE("a .vfVFXSequence without a bounds key loads as Auto")
    {
        resetBoundsTestRoot();

        json j;
        j["version"] = "1.2";
        j["uuid"] = "legacy-seq";
        j["name"] = "LegacySequence";
        j["steps"] = json::array();

        const fs::path path = boundsTestRoot() / "LegacySequence.vfVFXSequence";
        {
            std::ofstream file(path);
            REQUIRE(file.is_open());
            file << j.dump(4);
        }

        auto loadedOpt = vfx::VFXSequenceAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        CHECK(loadedOpt->bounds.mode == vfx::VFXBoundsMode::Auto);
        CHECK(loadedOpt->bounds.extents.x == doctest::Approx(0.0f));
    }
}
