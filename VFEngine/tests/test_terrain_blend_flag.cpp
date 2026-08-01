#include <doctest.h>
#include <material/MaterialTypes.hpp>
#include <material/TerrainBlendCurve.hpp>
#include <render/gpudriven/GPUDrivenTypes.hpp>
#include <render/material/MaterialPBRExtractor.hpp>

// ============================================================
// VK-1620 mesh-into-terrain blending — the per-material opt-in half.
//
// The gate is one ObjectFlags bit and the two parameters ride a spare uvec4 component, so nothing
// in the GPU layout grows. Both of those are claims about bit positions and struct slots that are
// invisible until something renders wrong, which is exactly what belongs in a CPU test.
// ============================================================

TEST_CASE("VK-1620 BlendToTerrain is bit 0 and collides with no other packed field")
{
    namespace F = render::gpudriven::ObjectFlags;

    // Bits 0-3 were the last unclaimed region in the flags word: every previously named constant
    // starts at 1<<4, and nothing (cull shader, draw packer, task/mesh stages) reads below bit 4.
    CHECK(F::BlendToTerrain == (1u << 0));

    const uint32_t categoryMask = 0xFu << 13;                                   // bits 13-16
    const uint32_t layerField   = F::LayerMask << F::LayerShift;                // bits 18-22
    const uint32_t shadingField = F::ShadingModelMask << F::ShadingModelShift;  // bits 23-24
    const uint32_t profileField = F::ProfileIndexMask << F::ProfileIndexShift;  // bits 25-31
    const uint32_t namedBits = F::AlphaMask | F::Translucent | F::NoCull | F::NoOcclude |
                               F::UniformScale | F::AdditiveBlend | F::MultiplyBlend |
                               F::TerrainTile | F::Selected | F::Billboard | F::Instanced |
                               F::ShadowStatic | F::FoliageWind;

    CHECK((F::BlendToTerrain & categoryMask) == 0u);
    CHECK((F::BlendToTerrain & layerField)   == 0u);
    CHECK((F::BlendToTerrain & shadingField) == 0u);
    CHECK((F::BlendToTerrain & profileField) == 0u);
    CHECK((F::BlendToTerrain & namedBits)    == 0u);
}

TEST_CASE("VK-1620 BlendToTerrain coexists with an already fully packed flags word")
{
    namespace F = render::gpudriven::ObjectFlags;

    uint32_t flags = 0;
    flags |= F::AlphaMask | F::Instanced | F::ShadowStatic | F::FoliageWind;
    flags |= (17u & F::LayerMask) << F::LayerShift;
    F::packShadingFlags(flags, F::ShadingModelToon, 93);
    const uint32_t before = flags;

    flags |= F::BlendToTerrain;

    // Setting the new bit must disturb nothing that was already packed.
    CHECK((flags & F::BlendToTerrain) != 0u);
    CHECK((flags & ~F::BlendToTerrain) == before);
    CHECK(((flags >> F::LayerShift) & F::LayerMask) == 17u);
    CHECK(((flags >> F::ShadingModelShift) & F::ShadingModelMask) == F::ShadingModelToon);
    CHECK(((flags >> F::ProfileIndexShift) & F::ProfileIndexMask) == 93u);
    CHECK((flags & F::FoliageWind) != 0u);
}

TEST_CASE("VK-1620 the material flag reaches the render side through the PBR extractor")
{
    // The authoring chain is MaterialData -> ExtractedPBRValues -> SubMeshMaterialInfo -> GPU flags.
    // This pins the seam where the material system hands off to the renderer.
    material::MaterialData mat;
    mat.blendToTerrain = true;
    mat.terrainBlendBand = 0.75f;
    mat.terrainBlendContrast = 3.0f;

    const auto pbr = render::mesh::MaterialPBRExtractor::extractPBRFromMaterial(mat, nullptr);
    CHECK(pbr.blendToTerrain);
    CHECK(pbr.terrainBlendBand == doctest::Approx(0.75f));
    CHECK(pbr.terrainBlendContrast == doctest::Approx(3.0f));

    // A material that never opted in must arrive off, with defaults that are themselves in range.
    material::MaterialData plain;
    const auto plainPbr = render::mesh::MaterialPBRExtractor::extractPBRFromMaterial(plain, nullptr);
    CHECK_FALSE(plainPbr.blendToTerrain);
    CHECK(material::clampTerrainBlendBand(plainPbr.terrainBlendBand) == plainPbr.terrainBlendBand);
    CHECK(material::clampTerrainBlendContrast(plainPbr.terrainBlendContrast) == plainPbr.terrainBlendContrast);
}

TEST_CASE("VK-1620 the extractor clamps, because the shader does not")
{
    // packTerrainBlendParams is the last stop before the values become halfs, and the shader reads
    // them without re-clamping. An out-of-range authored value therefore has to be caught here -
    // a contrast of 0 in particular would make pow() return 1 across the whole band.
    material::MaterialData mat;
    mat.blendToTerrain = true;
    mat.terrainBlendBand = -3.0f;
    mat.terrainBlendContrast = 0.0f;

    const auto pbr = render::mesh::MaterialPBRExtractor::extractPBRFromMaterial(mat, nullptr);
    CHECK(pbr.terrainBlendBand == doctest::Approx(material::MIN_TERRAIN_BLEND_BAND));
    CHECK(pbr.terrainBlendContrast == doctest::Approx(material::MIN_TERRAIN_BLEND_CONTRAST));

    material::MaterialData huge;
    huge.blendToTerrain = true;
    huge.terrainBlendBand = 1e6f;
    huge.terrainBlendContrast = 1e6f;
    const auto hugePbr = render::mesh::MaterialPBRExtractor::extractPBRFromMaterial(huge, nullptr);
    CHECK(hugePbr.terrainBlendBand == doctest::Approx(material::MAX_TERRAIN_BLEND_BAND));
    CHECK(hugePbr.terrainBlendContrast == doctest::Approx(material::MAX_TERRAIN_BLEND_CONTRAST));
}

TEST_CASE("VK-1620 defaults leave GPUObjectData's spare slots untouched")
{
    // The two parameters ride instanceData.z, whose only other writer sets the whole vector to
    // {INVALID_TEXTURE_INDEX, 0, 0, 0}. The packer must come AFTER that reset, and an object that
    // does not blend must leave .z at 0 rather than publishing a stale band.
    render::gpudriven::GPUObjectData obj{};
    obj.instanceData = glm::uvec4(render::gpudriven::INVALID_TEXTURE_INDEX, 0, 0, 0);
    CHECK(obj.instanceData.z == 0u);

    obj.instanceData.z = material::packTerrainBlendParams(material::DEFAULT_TERRAIN_BLEND_BAND,
                                                          material::DEFAULT_TERRAIN_BLEND_CONTRAST);
    CHECK(obj.instanceData.z != 0u);
    // .w carries the instance offset and .x an invalid-texture sentinel; neither may move.
    CHECK(obj.instanceData.w == 0u);
    CHECK(obj.instanceData.x == render::gpudriven::INVALID_TEXTURE_INDEX);

    // The struct must not have grown - the whole reason the parameters went into a spare slot.
    CHECK(sizeof(render::gpudriven::GPUObjectData) == 352);
    CHECK(sizeof(render::gpudriven::PerDrawData) == 256);
}
