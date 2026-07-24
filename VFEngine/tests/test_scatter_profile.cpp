#include <doctest.h>

// VK-1585: reusable .vfScatterProfile asset — shared serializer round-trip + backward compat.

#include "vegetation/VegetationScatterTypes.hpp"
#include "vegetation/ScatterProfileSerialization.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>

using namespace vegetation;

namespace
{
    ScatterProfile makeProfile()
    {
        ScatterProfile p;
        p.globalSeed = 4242;
        p.globalDensityScale = 0.75f;
        p.domain = ScatterDomain::Mesh;

        ScatterRule r;
        r.paletteEntryIndex = 3;
        r.density = 0.6f;
        r.spacing = 2.5f;
        r.positionJitter = 0.3f;
        r.alignToNormal = true;
        r.useSlopeMask = true;  r.slopeMinCos = 0.2f; r.slopeMaxCos = 0.9f;
        r.useHeightMask = true; r.heightMin = 5.0f; r.heightMax = 50.0f;
        r.useNoiseMask = true;  r.noiseFrequency = 0.2f; r.noiseThreshold = 0.4f; r.noiseSeed = 99;
        r.useLayerMask = true;  r.layerIndex = 7; r.layerWeightMin = 0.6f; r.invertLayer = true;
        r.useCurvatureMask = true; r.curvatureMin = 0.5f; r.curvatureMax = 12.0f;
        p.rules.push_back(r);

        ScatterRule r2;
        r2.paletteEntryIndex = 1;
        r2.density = 1.0f;
        p.rules.push_back(r2);
        return p;
    }

    void checkRuleEqual(const ScatterRule& a, const ScatterRule& b)
    {
        CHECK(a.paletteEntryIndex == b.paletteEntryIndex);
        CHECK(a.density == doctest::Approx(b.density));
        CHECK(a.spacing == doctest::Approx(b.spacing));
        CHECK(a.positionJitter == doctest::Approx(b.positionJitter));
        CHECK(a.alignToNormal == b.alignToNormal);
        CHECK(a.useSlopeMask == b.useSlopeMask);
        CHECK(a.slopeMinCos == doctest::Approx(b.slopeMinCos));
        CHECK(a.slopeMaxCos == doctest::Approx(b.slopeMaxCos));
        CHECK(a.useHeightMask == b.useHeightMask);
        CHECK(a.heightMin == doctest::Approx(b.heightMin));
        CHECK(a.heightMax == doctest::Approx(b.heightMax));
        CHECK(a.useNoiseMask == b.useNoiseMask);
        CHECK(a.noiseFrequency == doctest::Approx(b.noiseFrequency));
        CHECK(a.noiseThreshold == doctest::Approx(b.noiseThreshold));
        CHECK(a.noiseSeed == b.noiseSeed);
        CHECK(a.useLayerMask == b.useLayerMask);
        CHECK(a.layerIndex == b.layerIndex);
        CHECK(a.layerWeightMin == doctest::Approx(b.layerWeightMin));
        CHECK(a.invertLayer == b.invertLayer);
        CHECK(a.useCurvatureMask == b.useCurvatureMask);
        CHECK(a.curvatureMin == doctest::Approx(b.curvatureMin));
        CHECK(a.curvatureMax == doctest::Approx(b.curvatureMax));
    }
}

TEST_SUITE("ScatterProfileAsset")
{
    TEST_CASE("json round-trip preserves rules, domain, globals, curvature")
    {
        ScatterProfile in = makeProfile();
        nlohmann::json j;
        serializeScatterProfile(j, in);

        ScatterProfile out;
        deserializeScatterProfile(j, out);

        CHECK(out.globalSeed == in.globalSeed);
        CHECK(out.globalDensityScale == doctest::Approx(in.globalDensityScale));
        CHECK(out.domain == in.domain);
        REQUIRE(out.rules.size() == in.rules.size());
        for (size_t i = 0; i < in.rules.size(); ++i)
            checkRuleEqual(out.rules[i], in.rules[i]);
    }

    TEST_CASE("file round-trip (.vfScatterProfile)")
    {
        ScatterProfile in = makeProfile();
        auto path = (std::filesystem::temp_directory_path() / "vk1585.vfScatterProfile").string();
        REQUIRE(saveScatterProfileFile(path, in));

        ScatterProfile out;
        REQUIRE(loadScatterProfileFile(path, out));
        CHECK(out.domain == in.domain);
        CHECK(out.globalSeed == in.globalSeed);
        REQUIRE(out.rules.size() == in.rules.size());
        checkRuleEqual(out.rules[0], in.rules[0]);
        std::filesystem::remove(path);
    }

    TEST_CASE("missing keys keep defaults (backward compatible)")
    {
        // A legacy scatter object with no domain / curvature keys.
        nlohmann::json j;
        j["globalSeed"] = 7;
        j["globalDensityScale"] = 0.5f;
        nlohmann::json rj;
        rj["paletteEntryIndex"] = 2;
        rj["density"] = 0.9f;
        j["rules"] = nlohmann::json::array({rj});

        ScatterProfile out;
        deserializeScatterProfile(j, out);
        CHECK(out.globalSeed == 7u);
        CHECK(out.domain == ScatterDomain::Billboard);           // default
        REQUIRE(out.rules.size() == 1);
        CHECK(out.rules[0].paletteEntryIndex == 2u);
        CHECK(out.rules[0].useCurvatureMask == false);           // default
        CHECK(out.rules[0].curvatureMin == doctest::Approx(-100.0f)); // default
    }

    TEST_CASE("loadScatterProfileFile fails on a missing file")
    {
        ScatterProfile out;
        CHECK_FALSE(loadScatterProfileFile("does_not_exist_vk1585.vfScatterProfile", out));
    }
}
