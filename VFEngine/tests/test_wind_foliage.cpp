#include <doctest.h>
#include <material/MaterialTypes.hpp>
#include <material/MaterialAsset.hpp>
#include <render/gpudriven/GPUDrivenTypes.hpp>
#include <render/material/MaterialPBRExtractor.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

// ============================================================
// VK-1580 foliage wind — CPU-only unit tests (doctest).
// Global wind: a single FoliageWind gate bit + a material receiveWind flag; all sway
// parameters come from the shared grass WindSystem, so there is no per-object GPU data.
// ============================================================

TEST_CASE("FoliageWind is bit 8 and collides with no other packed field") {
    namespace F = render::gpudriven::ObjectFlags;

    CHECK(F::FoliageWind == (1u << 8));

    const uint32_t categoryMask = 0xFu << 13;                                   // bits 13-16
    const uint32_t layerField   = F::LayerMask << F::LayerShift;                // bits 18-22
    const uint32_t shadingField = F::ShadingModelMask << F::ShadingModelShift;  // bits 23-24
    const uint32_t profileField = F::ProfileIndexMask << F::ProfileIndexShift;  // bits 25-31
    const uint32_t namedBits = F::AlphaMask | F::Translucent | F::NoCull | F::NoOcclude |
                               F::UniformScale | F::AdditiveBlend | F::MultiplyBlend |
                               F::TerrainTile | F::Selected | F::Billboard | F::Instanced |
                               F::ShadowStatic;

    CHECK((F::FoliageWind & categoryMask) == 0u);
    CHECK((F::FoliageWind & layerField)   == 0u);
    CHECK((F::FoliageWind & shadingField) == 0u);
    CHECK((F::FoliageWind & profileField) == 0u);
    CHECK((F::FoliageWind & namedBits)    == 0u);
}

TEST_CASE("FoliageWind coexists with toon + layer + instanced packing") {
    namespace F = render::gpudriven::ObjectFlags;

    uint32_t flags = 0;
    flags |= F::Instanced;
    flags |= (7u & F::LayerMask) << F::LayerShift;         // render layer 7
    F::packShadingFlags(flags, F::ShadingModelToon, 42);   // bits 23-31
    const uint32_t before = flags;

    flags |= F::FoliageWind;                               // set the foliage gate

    // The gate bit is set and nothing else changed.
    CHECK((flags & F::FoliageWind) != 0u);
    CHECK((flags & ~F::FoliageWind) == before);
    CHECK((flags & F::Instanced) != 0u);
    CHECK(((flags >> F::LayerShift) & F::LayerMask) == 7u);
    CHECK(((flags >> F::ShadingModelShift) & F::ShadingModelMask) == F::ShadingModelToon);
    CHECK(((flags >> F::ProfileIndexShift) & F::ProfileIndexMask) == 42u);
}

TEST_CASE("MaterialPBRExtractor propagates receiveWind") {
    material::MaterialData windy = material::MaterialAsset::createDefault("WindyMat");
    windy.receiveWind = true;
    auto pbr = render::mesh::MaterialPBRExtractor::extractPBRFromMaterial(windy);
    CHECK(pbr.receiveWind == true);

    material::MaterialData rigid = material::MaterialAsset::createDefault("RigidMat");
    auto pbrRigid = render::mesh::MaterialPBRExtractor::extractPBRFromMaterial(rigid);
    CHECK(pbrRigid.receiveWind == false);
}

namespace {
    // Mirror of the mesh shader's height normalization (mesh_shader_gpudriven.glsl):
    //   vh = clamp((y - aabbMinY) / (aabbMaxY - aabbMinY), 0, 1);  factor = vh*vh.
    // A degenerate AABB (height <= 1e-4) disables wind (windActive = false).
    float foliageHeightFactor(float y, float aabbMinY, float aabbMaxY) {
        const float h = aabbMaxY - aabbMinY;
        if (h <= 1e-4f) return 0.0f;
        const float vh = std::clamp((y - aabbMinY) / h, 0.0f, 1.0f);
        return vh * vh;
    }

    // Mirror of wind_common.glsl::windHash used for per-instance phase decorrelation.
    float windHash(glm::vec2 p) {
        const float s = std::sin(glm::dot(p, glm::vec2(127.1f, 311.7f))) * 43758.5453123f;
        return s - std::floor(s);
    }
}

TEST_CASE("Foliage wind height factor anchors the root and saturates at the tip") {
    // Base anchored (0), tip full (1), midpoint quadratic (0.25).
    CHECK(foliageHeightFactor(0.0f, 0.0f, 4.0f) == doctest::Approx(0.0f));
    CHECK(foliageHeightFactor(4.0f, 0.0f, 4.0f) == doctest::Approx(1.0f));
    CHECK(foliageHeightFactor(2.0f, 0.0f, 4.0f) == doctest::Approx(0.25f));

    // Out-of-bounds clamps.
    CHECK(foliageHeightFactor(-1.0f, 0.0f, 4.0f) == doctest::Approx(0.0f));
    CHECK(foliageHeightFactor(9.0f, 0.0f, 4.0f) == doctest::Approx(1.0f));

    // Non-zero base offset (mesh authored off the origin).
    CHECK(foliageHeightFactor(5.0f, 5.0f, 15.0f) == doctest::Approx(0.0f));
    CHECK(foliageHeightFactor(10.0f, 5.0f, 15.0f) == doctest::Approx(0.25f));

    // Degenerate AABB => no wind (div-by-zero guard).
    CHECK(foliageHeightFactor(3.0f, 3.0f, 3.0f) == doctest::Approx(0.0f));
}

TEST_CASE("windHash is deterministic, decorrelated, and in [0,1)") {
    const glm::vec2 a(12.0f, 7.0f);
    const glm::vec2 b(13.0f, 7.0f);

    CHECK(windHash(a) == doctest::Approx(windHash(a)));   // deterministic
    CHECK(windHash(a) != doctest::Approx(windHash(b)));   // neighbors decorrelate

    for (int i = 0; i < 64; ++i) {
        const float h = windHash(glm::vec2(static_cast<float>(i) * 1.7f, static_cast<float>(i) * 3.1f));
        CHECK(h >= 0.0f);
        CHECK(h < 1.0f);
    }
}
