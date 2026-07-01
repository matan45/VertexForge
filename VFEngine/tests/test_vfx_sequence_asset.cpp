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
#include <vfx/VFXTypes.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetDatabase.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

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
        data.version = vfx::VFX_SEQUENCE_FORMAT_VERSION;
        data.uuid = "1234567890";
        data.name = "FireballCombo";

        // VK-1451 timeline controls.
        data.seed = 777u;
        data.playbackRate = 1.5f;
        data.fixedStep = 1.0f / 60.0f;
        data.prewarm = 0.25f;
        vfx::VFXSequenceEventMarker hitMarker{0.20f, "OnHit"};
        hitMarker.payload.position = glm::vec3(1.0f, 2.0f, 3.0f);
        hitMarker.payload.color = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);
        hitMarker.payload.scalar = 42.0f;
        hitMarker.payload.custom.push_back(vfx::VFXParamOverride{"startColor", glm::vec4(1.0f, 0.5f, 0.25f, 1.0f)});
        data.eventMarkers.push_back(hitMarker);
        data.eventMarkers.push_back(vfx::VFXSequenceEventMarker{0.80f, "OnEnd"});

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
            step.overrides.push_back(vfx::VFXParamOverride{"spawnRate", 50.0f});
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
            step.overrides.push_back(vfx::VFXParamOverride{"startColor", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)});
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
            step.overrides.push_back(vfx::VFXParamOverride{"lifetime", 3.0f});
            step.overrides.push_back(vfx::VFXParamOverride{"startSpeed", 12.5f});
            step.overrides.push_back(vfx::VFXParamOverride{"emitDirection", glm::vec3(0.0f, 1.0f, 0.0f)});
            data.steps.push_back(step);
        }

        return data;
    }

    void checkValueEqual(const vfx::VFXPropertyValue& a, const vfx::VFXPropertyValue& b)
    {
        REQUIRE(a.index() == b.index());
        if (const auto* va = std::get_if<float>(&a))
            CHECK(*va == doctest::Approx(std::get<float>(b)));
        else if (const auto* va = std::get_if<glm::vec2>(&a))
        {
            const auto& vb = std::get<glm::vec2>(b);
            CHECK(va->x == doctest::Approx(vb.x));
            CHECK(va->y == doctest::Approx(vb.y));
        }
        else if (const auto* va = std::get_if<glm::vec3>(&a))
        {
            const auto& vb = std::get<glm::vec3>(b);
            CHECK(va->x == doctest::Approx(vb.x));
            CHECK(va->y == doctest::Approx(vb.y));
            CHECK(va->z == doctest::Approx(vb.z));
        }
        else if (const auto* va = std::get_if<glm::vec4>(&a))
        {
            const auto& vb = std::get<glm::vec4>(b);
            CHECK(va->x == doctest::Approx(vb.x));
            CHECK(va->y == doctest::Approx(vb.y));
            CHECK(va->z == doctest::Approx(vb.z));
            CHECK(va->w == doctest::Approx(vb.w));
        }
        else if (const auto* va = std::get_if<int32_t>(&a))
            CHECK(*va == std::get<int32_t>(b));
        else if (const auto* va = std::get_if<bool>(&a))
            CHECK(*va == std::get<bool>(b));
        else if (const auto* va = std::get_if<std::string>(&a))
            CHECK(*va == std::get<std::string>(b));
    }

    void checkOverrideEqual(const vfx::VFXParamOverride& a, const vfx::VFXParamOverride& b)
    {
        CHECK(a.name == b.name);
        checkValueEqual(a.value, b.value);
    }

    void checkPayloadEqual(const vfx::VFXCuePayload& a, const vfx::VFXCuePayload& b)
    {
        CHECK(a.position.has_value() == b.position.has_value());
        if (a.position)
        {
            CHECK(a.position->x == doctest::Approx(b.position->x));
            CHECK(a.position->y == doctest::Approx(b.position->y));
            CHECK(a.position->z == doctest::Approx(b.position->z));
        }
        CHECK(a.color.has_value() == b.color.has_value());
        if (a.color)
        {
            CHECK(a.color->x == doctest::Approx(b.color->x));
            CHECK(a.color->y == doctest::Approx(b.color->y));
            CHECK(a.color->z == doctest::Approx(b.color->z));
            CHECK(a.color->w == doctest::Approx(b.color->w));
        }
        CHECK(a.scalar.has_value() == b.scalar.has_value());
        if (a.scalar)
            CHECK(*a.scalar == doctest::Approx(*b.scalar));
        REQUIRE(a.custom.size() == b.custom.size());
        for (size_t i = 0; i < a.custom.size(); ++i)
            checkOverrideEqual(a.custom[i], b.custom[i]);
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

        REQUIRE(a.overrides.size() == b.overrides.size());
        for (size_t i = 0; i < a.overrides.size(); ++i)
            checkOverrideEqual(a.overrides[i], b.overrides[i]);
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

        CHECK(loaded.version == vfx::VFX_SEQUENCE_FORMAT_VERSION);
        CHECK(loaded.uuid == original.uuid);
        CHECK(loaded.name == original.name);

        REQUIRE(loaded.steps.size() == original.steps.size());
        for (size_t i = 0; i < original.steps.size(); ++i)
        {
            checkStepEqual(loaded.steps[i], original.steps[i]);
        }

        // VK-1451 timeline controls round-trip.
        CHECK(loaded.seed == original.seed);
        CHECK(loaded.playbackRate == doctest::Approx(original.playbackRate));
        CHECK(loaded.fixedStep == doctest::Approx(original.fixedStep));
        CHECK(loaded.prewarm == doctest::Approx(original.prewarm));
        REQUIRE(loaded.eventMarkers.size() == original.eventMarkers.size());
        for (size_t i = 0; i < original.eventMarkers.size(); ++i)
        {
            CHECK(loaded.eventMarkers[i].time == doctest::Approx(original.eventMarkers[i].time));
            CHECK(loaded.eventMarkers[i].cueName == original.eventMarkers[i].cueName);
            checkPayloadEqual(loaded.eventMarkers[i].payload, original.eventMarkers[i].payload);
        }
    }

    TEST_CASE("legacy scalar/vector override arrays migrate to typed overrides")
    {
        resetSequenceTestRoot();

        json j;
        j["version"] = "1.1";
        j["uuid"] = "legacy-overrides";
        j["name"] = "LegacyOverrides";

        json step;
        step["vfxRef"] = "00000000aaaa1111";
        step["label"] = "legacy";
        step["scalarOverrides"] = json::array({
            json::array({"spawnRate", 12.5f}),
            json::array({"renderMode", 2.0f}),
            json::array({"collisionEnabled", 1.0f}),
            json::array({"unknownScalar", 7.0f})
        });
        step["vectorOverrides"] = json::array({
            json::array({"emitDirection", json::array({0.0f, 1.0f, 0.0f, 9.0f})}),
            json::array({"startColor", json::array({1.0f, 0.0f, 0.5f, 1.0f})}),
            json::array({"unknownVector", json::array({1.0f, 2.0f, 3.0f, 4.0f})})
        });
        j["steps"] = json::array({step});

        const fs::path path = sequenceTestRoot() / "LegacyOverrides.vfVFXSequence";
        {
            std::ofstream file(path);
            REQUIRE(file.is_open());
            file << j.dump(4);
        }

        auto loadedOpt = vfx::VFXSequenceAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        REQUIRE(loadedOpt->steps.size() == 1);
        const auto& overrides = loadedOpt->steps[0].overrides;
        REQUIRE(overrides.size() == 7);

        CHECK(overrides[0].name == "spawnRate");
        CHECK(std::get<float>(overrides[0].value) == doctest::Approx(12.5f));
        CHECK(std::get<int32_t>(overrides[1].value) == 2);
        CHECK(std::get<bool>(overrides[2].value) == true);
        CHECK(std::get<float>(overrides[3].value) == doctest::Approx(7.0f));
        CHECK(std::get<glm::vec3>(overrides[4].value).y == doctest::Approx(1.0f));
        CHECK(std::get<glm::vec4>(overrides[5].value).z == doctest::Approx(0.5f));
        CHECK(std::get<glm::vec4>(overrides[6].value).w == doctest::Approx(4.0f));
    }

    TEST_CASE("a 1.0 file without timeline-control keys loads with safe defaults")
    {
        resetSequenceTestRoot();

        // A legacy 1.0 file: only version/uuid/name/steps, no VK-1451 keys.
        json j;
        j["version"] = "1.0";
        j["uuid"] = "42";
        j["name"] = "Legacy";

        json step;
        step["vfxRef"] = "00000000aaaa1111";
        step["label"] = "only";
        step["startTime"] = 0.0f;
        j["steps"] = json::array({step});

        const fs::path path = sequenceTestRoot() / "Legacy.vfVFXSequence";
        {
            std::ofstream file(path);
            REQUIRE(file.is_open());
            file << j.dump(4);
        }

        auto loadedOpt = vfx::VFXSequenceAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        const vfx::VFXSequenceData& loaded = *loadedOpt;

        REQUIRE(loaded.steps.size() == 1);
        CHECK(loaded.seed == 0u);
        CHECK(loaded.playbackRate == doctest::Approx(1.0f));
        CHECK(loaded.fixedStep == doctest::Approx(0.0f));
        CHECK(loaded.prewarm == doctest::Approx(0.0f));
        CHECK(loaded.eventMarkers.empty());
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

    TEST_CASE("a malformed override value defaults instead of failing the whole load (VK-1460)")
    {
        resetSequenceTestRoot();

        // Build a valid step carrying a single Vec3 override, save it (so the typed-override
        // format + type string come from the codec itself), then corrupt ONLY that override's
        // value into a wrong-shaped array of strings. The Vec3 branch passes the
        // is_array()/size()>=3 gate but throws on element .get<float>().
        vfx::VFXSequenceData data;
        data.version = vfx::VFX_SEQUENCE_FORMAT_VERSION;
        data.uuid = "malformed-override";
        data.name = "MalformedOverride";
        {
            vfx::VFXSequenceStep step;
            step.vfxRef = asset::AssetRef::fromHexString("00000000aaaa1111");
            step.label = "step0";
            step.overrides.push_back(vfx::VFXParamOverride{"myVec", glm::vec3(1.0f, 2.0f, 3.0f)});
            data.steps.push_back(step);
        }

        const fs::path path = sequenceTestRoot() / "MalformedOverride.vfVFXSequence";
        REQUIRE(vfx::VFXSequenceAsset::save(data, path.string()));

        json j;
        {
            std::ifstream in(path);
            REQUIRE(in.is_open());
            in >> j;
        }
        REQUIRE(j["steps"].is_array());
        REQUIRE(j["steps"][0]["overrides"].is_array());
        REQUIRE(j["steps"][0]["overrides"][0].is_array());
        j["steps"][0]["overrides"][0][2] = json::array({"x", "y", "z"}); // was [1,2,3]
        {
            std::ofstream out(path);
            REQUIRE(out.is_open());
            out << j.dump(4);
        }

        // Before VK-1460 the json::exception escaped to the file-level catch and load()
        // returned nullopt — the ENTIRE sequence failed over one bad value. Now the value
        // defaults (matching VFXAsset::deserializePropertyValue -> 0.0f) and the step loads.
        auto loadedOpt = vfx::VFXSequenceAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        REQUIRE(loadedOpt->steps.size() == 1);
        REQUIRE(loadedOpt->steps[0].overrides.size() == 1);
        CHECK(loadedOpt->steps[0].overrides[0].name == "myVec");
        CHECK(std::holds_alternative<float>(loadedOpt->steps[0].overrides[0].value));
        CHECK(std::get<float>(loadedOpt->steps[0].overrides[0].value) == doctest::Approx(0.0f));
    }

    TEST_CASE("load of a non-existent path returns nullopt")
    {
        resetSequenceTestRoot();

        const fs::path missing = sequenceTestRoot() / "does_not_exist.vfVFXSequence";
        auto loadedOpt = vfx::VFXSequenceAsset::load(missing.string());
        CHECK_FALSE(loadedOpt.has_value());
    }
}
