// CPU-only serialization coverage for the billboard-animation fields (Phase 1).
//   1. Backward compatibility: a billboard JSON object written before the
//      animation fields existed must deserialize to the static-billboard
//      defaults (cols=1, rows=1, every rate/scroll/pulse/spin/animStartTime=0,
//      worldMarker=false). deserializeBillboard uses j.value(key, default), so a
//      missing key must fall back to the default — older scenes keep rendering.
//   2. Round-trip: a component with non-default animation values must serialize
//      the keys and deserialize back to the same values. Catches a key-name
//      mismatch between serialize and deserialize.
//
// Uses the static SceneSerialization::serialize/deserializeBillboard directly
// (the exact mapping seam) — no SceneGraphSystem or graphics layer required.

#include <doctest.h>

#include <serialization/SceneSerialization.hpp>
#include <components/Components.hpp>
#include <nlohmann/json.hpp>

namespace
{
    using json = nlohmann::json;
    using serialization::SceneSerialization;
}

TEST_SUITE("BillboardSerialization")
{
    TEST_CASE("billboard JSON without animation keys deserializes to static-billboard defaults")
    {
        // A pre-animation billboard payload: only the original fields are present.
        json j;
        j["iconType"] = "billboard";
        j["atlasIndex"] = 7u;
        j["sizeMode"] = "worldSpace";
        j["size"] = json::array({2.0f, 2.0f});
        j["colorTint"] = json::array({1.0f, 1.0f, 1.0f, 1.0f});
        j["editorOnly"] = false;
        j["selectable"] = true;

        // Pre-seed the target with non-default animation values to prove the
        // deserializer overwrites them with the defaults rather than leaving them.
        components::BillboardComponent billboard;
        billboard.flipbookColumns = 9u;
        billboard.flipbookRows = 9u;
        billboard.flipbookFrameRate = 99.0f;
        billboard.scrollU = 9.0f;
        billboard.scrollV = 9.0f;
        billboard.pulseAmplitude = 9.0f;
        billboard.pulseFrequency = 9.0f;
        billboard.spinSpeed = 9.0f;
        billboard.animStartTime = 9.0f;
        billboard.worldMarker = true;

        SceneSerialization::deserializeBillboard(j, billboard);

        CHECK(billboard.flipbookColumns == 1u);
        CHECK(billboard.flipbookRows == 1u);
        CHECK(billboard.flipbookFrameRate == doctest::Approx(0.0f));
        CHECK(billboard.scrollU == doctest::Approx(0.0f));
        CHECK(billboard.scrollV == doctest::Approx(0.0f));
        CHECK(billboard.pulseAmplitude == doctest::Approx(0.0f));
        CHECK(billboard.pulseFrequency == doctest::Approx(0.0f));
        CHECK(billboard.spinSpeed == doctest::Approx(0.0f));
        CHECK(billboard.animStartTime == doctest::Approx(0.0f));
        CHECK(billboard.worldMarker == false);

        // Original fields still load correctly alongside the defaults.
        CHECK(billboard.atlasIndex == 7u);
        CHECK(billboard.sizeMode == components::BillboardSizeMode::WorldSpace);
        CHECK(billboard.editorOnly == false);
    }

    TEST_CASE("animation fields round-trip through serialize/deserialize")
    {
        components::BillboardComponent source;
        source.iconType = components::BillboardIconType::Billboard;
        source.flipbookColumns = 4u;
        source.flipbookRows = 8u;
        source.flipbookFrameRate = 30.0f;
        source.scrollU = 0.5f;
        source.scrollV = -0.25f;
        source.pulseAmplitude = 0.6f;
        source.pulseFrequency = 2.0f;
        source.spinSpeed = 1.25f;
        source.animStartTime = 5.0f;
        source.worldMarker = true;

        const json j = SceneSerialization::serializeBillboard(source);

        // Keys are present with the authored values.
        REQUIRE(j.contains("flipbookColumns"));
        REQUIRE(j.contains("flipbookRows"));
        REQUIRE(j.contains("flipbookFrameRate"));
        REQUIRE(j.contains("scrollU"));
        REQUIRE(j.contains("scrollV"));
        REQUIRE(j.contains("pulseAmplitude"));
        REQUIRE(j.contains("pulseFrequency"));
        REQUIRE(j.contains("spinSpeed"));
        REQUIRE(j.contains("animStartTime"));
        REQUIRE(j.contains("worldMarker"));
        CHECK(j["flipbookColumns"].get<uint32_t>() == 4u);
        CHECK(j["flipbookRows"].get<uint32_t>() == 8u);
        CHECK(j["worldMarker"].get<bool>() == true);

        components::BillboardComponent loaded;
        SceneSerialization::deserializeBillboard(j, loaded);

        CHECK(loaded.flipbookColumns == 4u);
        CHECK(loaded.flipbookRows == 8u);
        CHECK(loaded.flipbookFrameRate == doctest::Approx(30.0f));
        CHECK(loaded.scrollU == doctest::Approx(0.5f));
        CHECK(loaded.scrollV == doctest::Approx(-0.25f));
        CHECK(loaded.pulseAmplitude == doctest::Approx(0.6f));
        CHECK(loaded.pulseFrequency == doctest::Approx(2.0f));
        CHECK(loaded.spinSpeed == doctest::Approx(1.25f));
        CHECK(loaded.animStartTime == doctest::Approx(5.0f));
        CHECK(loaded.worldMarker == true);
    }
}
