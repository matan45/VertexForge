// CPU-only codec coverage for the .vfVFXSequence asset (VK-1425 Part B).
//
//   1. Round-trip: a VFXSequenceData with several steps covering distinct GUID
//      refs, transforms, loop flags, sockets, time- vs cue-driven triggering,
//      scalar/vector overrides and mixed stop modes must save and reload
//      field-by-field identical (including step order and GUID equality).
//   2. Malformed-step tolerance: a hand-written JSON file with one bad step
//      among good ones loads, keeps the good steps, and drops the bad one
//      without throwing.
//   3. Missing file: load() of a non-existent path returns std::nullopt.
//
// The seam is the public VFXSequenceAsset::save / load over a temp file. No
// graphics layer, no Vulkan device. AssetDatabase is cleared so vfxRef GUIDs do
// not resolve to paths and therefore round-trip purely as hex GUIDs.

#include <doctest.h>

#include <vfx/VFXSequenceAsset.hpp>
#include <vfx/VFXSequenceTypes.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetDatabase.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path sequenceTestRoot()
    {
        return fs::temp_directory_path() / "vf_vfx_sequence_asset_tests";
    }

    void resetSequenceTestRoot()
    {
        std::error_code ec;
        fs::remove_all(sequenceTestRoot(), ec);
        fs::create_directories(sequenceTestRoot(), ec);
        asset::AssetDatabase::instance().clear();
    }

    vfx::VFXSequenceData makeSampleSequence()
    {
        vfx::VFXSequenceData data;
        data.version = "1.0";
        data.uuid = "1234567890";
        data.name = "FireballCombo";

        // Step 0: time-driven, looping, with a socket and a scalar override.
        {
            vfx::VFXSequenceStep step;
            step.vfxRef = asset::AssetRef::fromHexString("00000000aaaa1111");
            step.label = "muzzle";
            step.startTime = 0.10f;
            step.cueName = ""; // time-driven
            step.localPosition = glm::vec3(1.0f, 2.0f, 3.0f);
            step.localEulerDeg = glm::vec3(10.0f, 20.0f, 30.0f);
            step.localScale = glm::vec3(2.0f, 2.0f, 2.0f);
            step.loop = true;
            step.duration = 0.0f;
            step.stopMode = vfx::VFXStepStopMode::PlayToCompletion;
            step.socketName = "hand_R";
            step.scalarOverrides.emplace_back("spawnRate", 50.0f);
            data.steps.push_back(step);
        }

        // Step 1: cue-driven, not looping, no socket, with a vector override and
        // a forced stop-after-duration.
        {
            vfx::VFXSequenceStep step;
            step.vfxRef = asset::AssetRef::fromHexString("ffffffff22223333");
            step.label = "impact";
            step.startTime = 0.0f;
            step.cueName = "OnHit"; // cue-driven
            step.localPosition = glm::vec3(-4.0f, 0.5f, 7.25f);
            step.localEulerDeg = glm::vec3(0.0f, 90.0f, 0.0f);
            step.localScale = glm::vec3(1.0f, 1.0f, 1.0f);
            step.loop = false;
            step.duration = 1.5f;
            step.stopMode = vfx::VFXStepStopMode::StopAfterDuration;
            step.socketName = ""; // no socket
            step.vectorOverrides.emplace_back("startColor", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
            data.steps.push_back(step);
        }

        // Step 2: both a scalar and a vector override, distinct GUID.
        {
            vfx::VFXSequenceStep step;
            step.vfxRef = asset::AssetRef::fromHexString("0123456789abcdef");
            step.label = "trail";
            step.startTime = 0.35f;
            step.cueName = "";
            step.localPosition = glm::vec3(0.0f, 0.0f, 0.0f);
            step.localEulerDeg = glm::vec3(45.0f, 0.0f, 0.0f);
            step.localScale = glm::vec3(0.5f, 0.75f, 1.25f);
            step.loop = true;
            step.duration = 2.0f;
            step.stopMode = vfx::VFXStepStopMode::StopAfterDuration;
            step.socketName = "spine_01";
            step.scalarOverrides.emplace_back("lifetime", 3.0f);
            step.scalarOverrides.emplace_back("startSpeed", 12.5f);
            step.vectorOverrides.emplace_back("emitDirection", glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
            data.steps.push_back(step);
        }

        return data;
    }

    void checkStepEqual(const vfx::VFXSequenceStep& a, const vfx::VFXSequenceStep& b)
    {
        CHECK(a.vfxRef.toHexString() == b.vfxRef.toHexString());
        CHECK(a.label == b.label);
        CHECK(a.startTime == doctest::Approx(b.startTime));
        CHECK(a.cueName == b.cueName);
        CHECK(a.localPosition.x == doctest::Approx(b.localPosition.x));
        CHECK(a.localPosition.y == doctest::Approx(b.localPosition.y));
        CHECK(a.localPosition.z == doctest::Approx(b.localPosition.z));
        CHECK(a.localEulerDeg.x == doctest::Approx(b.localEulerDeg.x));
        CHECK(a.localEulerDeg.y == doctest::Approx(b.localEulerDeg.y));
        CHECK(a.localEulerDeg.z == doctest::Approx(b.localEulerDeg.z));
        CHECK(a.localScale.x == doctest::Approx(b.localScale.x));
        CHECK(a.localScale.y == doctest::Approx(b.localScale.y));
        CHECK(a.localScale.z == doctest::Approx(b.localScale.z));
        CHECK(a.loop == b.loop);
        CHECK(a.duration == doctest::Approx(b.duration));
        CHECK(a.stopMode == b.stopMode);
        CHECK(a.socketName == b.socketName);

        REQUIRE(a.scalarOverrides.size() == b.scalarOverrides.size());
        for (size_t i = 0; i < a.scalarOverrides.size(); ++i)
        {
            CHECK(a.scalarOverrides[i].first == b.scalarOverrides[i].first);
            CHECK(a.scalarOverrides[i].second == doctest::Approx(b.scalarOverrides[i].second));
        }

        REQUIRE(a.vectorOverrides.size() == b.vectorOverrides.size());
        for (size_t i = 0; i < a.vectorOverrides.size(); ++i)
        {
            CHECK(a.vectorOverrides[i].first == b.vectorOverrides[i].first);
            CHECK(a.vectorOverrides[i].second.x == doctest::Approx(b.vectorOverrides[i].second.x));
            CHECK(a.vectorOverrides[i].second.y == doctest::Approx(b.vectorOverrides[i].second.y));
            CHECK(a.vectorOverrides[i].second.z == doctest::Approx(b.vectorOverrides[i].second.z));
            CHECK(a.vectorOverrides[i].second.w == doctest::Approx(b.vectorOverrides[i].second.w));
        }
    }
}

TEST_SUITE("VFXSequenceAsset")
{
    TEST_CASE("round-trips all step fields, order and GUIDs through save/load")
    {
        resetSequenceTestRoot();

        const vfx::VFXSequenceData original = makeSampleSequence();
        const fs::path path = sequenceTestRoot() / "FireballCombo.vfVFXSequence";

        REQUIRE(vfx::VFXSequenceAsset::save(original, path.string()));

        auto loadedOpt = vfx::VFXSequenceAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        const vfx::VFXSequenceData& loaded = *loadedOpt;

        CHECK(loaded.version == original.version);
        CHECK(loaded.uuid == original.uuid);
        CHECK(loaded.name == original.name);

        REQUIRE(loaded.steps.size() == original.steps.size());
        for (size_t i = 0; i < original.steps.size(); ++i)
        {
            checkStepEqual(loaded.steps[i], original.steps[i]);
        }
    }

    TEST_CASE("malformed step is skipped, good steps survive, no throw")
    {
        resetSequenceTestRoot();

        // Hand-written file: a good step, a malformed step (no vfxRef), another
        // good step. The codec must keep the two good steps and drop the middle.
        json j;
        j["version"] = "1.0";
        j["uuid"] = "999";
        j["name"] = "Mixed";

        json good0;
        good0["vfxRef"] = "00000000aaaa1111";
        good0["label"] = "first";
        good0["startTime"] = 0.0f;

        json bad; // missing vfxRef entirely
        bad["label"] = "broken";
        bad["startTime"] = 0.5f;

        json good1;
        good1["vfxRef"] = "ffffffff22223333";
        good1["label"] = "third";
        good1["startTime"] = 1.0f;

        j["steps"] = json::array({good0, bad, good1});

        const fs::path path = sequenceTestRoot() / "Mixed.vfVFXSequence";
        {
            std::ofstream file(path);
            REQUIRE(file.is_open());
            file << j.dump(4);
        }

        auto loadedOpt = vfx::VFXSequenceAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        const vfx::VFXSequenceData& loaded = *loadedOpt;

        REQUIRE(loaded.steps.size() == 2);
        CHECK(loaded.steps[0].label == "first");
        CHECK(loaded.steps[0].vfxRef.toHexString() == "00000000aaaa1111");
        CHECK(loaded.steps[1].label == "third");
        CHECK(loaded.steps[1].vfxRef.toHexString() == "ffffffff22223333");
    }

    TEST_CASE("load of a non-existent path returns nullopt")
    {
        resetSequenceTestRoot();

        const fs::path missing = sequenceTestRoot() / "does_not_exist.vfVFXSequence";
        auto loadedOpt = vfx::VFXSequenceAsset::load(missing.string());
        CHECK_FALSE(loadedOpt.has_value());
    }
}
