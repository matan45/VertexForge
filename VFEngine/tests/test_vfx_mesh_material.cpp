#include <doctest.h>
#include "render/vfx/mesh/VFXMeshMaterialResolver.hpp"
#include "render/material/MaterialPBRExtractor.hpp"

// VK-1526: VFX mesh-render particles source PBR shading from a referenced .vfMat/.vfMatInstance.
// resolveVFXMeshMaterial() is the pure CPU seam mapping the material's extracted PBR values into the
// VFX-representable subset (4-map ORM-packed + scalars + tint), mirroring the VK-1486 terrain resolver.
// These CPU-only tests pin its semantics (no Vulkan, no device).

using render::mesh::ExtractedPBRValues;
using render::vfx::resolveVFXMeshMaterial;

TEST_CASE("VFXMeshMaterialResolver: no material")
{
    SUBCASE("nullptr => hasMaterial false, defaults")
    {
        auto out = resolveVFXMeshMaterial(nullptr);
        CHECK(out.hasMaterial == false);
        CHECK(out.usesORM == false);
        CHECK(out.warnSeparateOrmMaps == false);
        CHECK(out.albedoPath.empty());
        CHECK(out.normalPath.empty());
        CHECK(out.ormPath.empty());
        CHECK(out.emissivePath.empty());
        CHECK(out.metallic == doctest::Approx(0.0f));
        CHECK(out.roughness == doctest::Approx(0.5f));
        CHECK(out.ao == doctest::Approx(1.0f));
        CHECK(out.emissionStrength == doctest::Approx(0.0f));
        CHECK(out.albedoTint.r == doctest::Approx(1.0f));
        CHECK(out.albedoTint.a == doctest::Approx(1.0f));
    }

    SUBCASE("failed extraction (empty materialPath) => hasMaterial false")
    {
        ExtractedPBRValues pbr; // materialPath left empty = extraction failure sentinel
        pbr.albedoTexturePath = "albedo.vfImage";
        auto out = resolveVFXMeshMaterial(&pbr);
        CHECK(out.hasMaterial == false);
        CHECK(out.albedoPath.empty()); // nothing copied when extraction failed
    }
}

TEST_CASE("VFXMeshMaterialResolver: ORM-packed material")
{
    ExtractedPBRValues pbr;
    pbr.materialPath = "crystal.vfMat";
    pbr.albedoTexturePath = "crystal_albedo.vfImage";
    pbr.normalTexturePath = "crystal_normal.vfImage";
    pbr.ormTexturePath = "crystal_orm.vfImage";
    pbr.emissionTexturePath = "crystal_emissive.vfImage";
    pbr.metallic = 0.25f;
    pbr.roughness = 0.4f;
    pbr.ao = 0.8f;
    pbr.emission = 2.5f;
    pbr.albedo = glm::vec4(0.9f, 0.2f, 0.2f, 1.0f);

    auto out = resolveVFXMeshMaterial(&pbr);

    CHECK(out.hasMaterial == true);
    CHECK(out.usesORM == true);
    CHECK(out.warnSeparateOrmMaps == false);
    CHECK(out.albedoPath == "crystal_albedo.vfImage");
    CHECK(out.normalPath == "crystal_normal.vfImage");
    CHECK(out.ormPath == "crystal_orm.vfImage");
    CHECK(out.emissivePath == "crystal_emissive.vfImage");
    CHECK(out.metallic == doctest::Approx(0.25f));
    CHECK(out.roughness == doctest::Approx(0.4f));
    CHECK(out.ao == doctest::Approx(0.8f));
    CHECK(out.emissionStrength == doctest::Approx(2.5f));
    CHECK(out.albedoTint.r == doctest::Approx(0.9f));
    CHECK(out.albedoTint.g == doctest::Approx(0.2f));
}

TEST_CASE("VFXMeshMaterialResolver: separate metallic/roughness/AO maps warn")
{
    ExtractedPBRValues pbr;
    pbr.materialPath = "metal.vfMat";
    pbr.albedoTexturePath = "metal_albedo.vfImage";
    pbr.metallicTexturePath = "metal_metallic.vfImage";
    pbr.roughnessTexturePath = "metal_roughness.vfImage";
    // no ormTexturePath => usesORM() false

    auto out = resolveVFXMeshMaterial(&pbr);

    CHECK(out.hasMaterial == true);
    CHECK(out.usesORM == false);
    CHECK(out.warnSeparateOrmMaps == true); // VFX packs ORM only; separate maps can't be represented
    CHECK(out.ormPath.empty());
}

TEST_CASE("VFXMeshMaterialResolver: scalar-only material (no textures)")
{
    ExtractedPBRValues pbr;
    pbr.materialPath = "scalar.vfMat";
    pbr.metallic = 1.0f;
    pbr.roughness = 0.1f;
    pbr.albedo = glm::vec4(0.1f, 0.5f, 0.9f, 1.0f);
    // no texture paths at all

    auto out = resolveVFXMeshMaterial(&pbr);

    CHECK(out.hasMaterial == true);
    CHECK(out.usesORM == false);
    CHECK(out.warnSeparateOrmMaps == false); // no separate M/R/AO maps => no warning
    CHECK(out.albedoPath.empty());
    CHECK(out.metallic == doctest::Approx(1.0f));
    CHECK(out.roughness == doctest::Approx(0.1f));
    CHECK(out.albedoTint.b == doctest::Approx(0.9f));
}
