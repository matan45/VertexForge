// VK-1209 — terrain Runtime Virtual Texture bake shader. Renders one quad per
// (page, overlapping terrain tile) with an orthographic mapping of the page's world
// XZ rect into the page's atlas tile, and writes the SAME terrain splat composite
// (terrain_material_generated.glsl, included verbatim) into 2 or 4 MRT planes:
//   plane 0 = albedo.rgb + emissionStrength.a
//   plane 1 = ao.r + roughness.g + metallic.b
//   plane 2 = encoded tangent-space normal.rgb (detail maps only)
//   plane 3 = linear HDR emission.rgb (detail maps only)
// The main terrain pass then samples these texels instead of compositing 8 layers
// (up to 32 bindless samples with detail maps) per fragment. Bake uses the XZ projection only
// (the composite is baked flat, top-down).

#type VERTEX
#version 460 core

layout(push_constant) uniform BakePC {
    vec2 pageWorldMin;    // page footprint origin (world XZ)
    vec2 pageWorldSize;   // page footprint size
    vec2 quadWorldMin;    // this draw's quad = page ∩ tile (world XZ)
    vec2 quadWorldSize;
    vec2 tileWorldMin;    // terrain tile origin (world XZ) for per-tile UV
    float tileWorldSize;
    uint  fragTileIndex;  // terrain tile GPU index
    float textureScale;   // world-space UV tiling scale
    // VK-1620 world-height plane normalization. Deliberately the terrain's AUTHORED height range
    // (TerrainTileConfig::minHeight/maxHeight), not the bounds of whatever tiles happen to be
    // loaded: pages bake once and are then read for as long as they stay resident, so a range that
    // drifted with streaming would leave every already-baked page decoding to a wrong world Y.
    float heightMin;
    float invHeightRange;
    float pad2;
} pc;

layout(location = 0) out vec2 vWorldXZ;
layout(location = 1) out vec2 vTileUV;
layout(location = 2) out flat uint vTileIndex;

void main()
{
    // Two-triangle quad from gl_VertexIndex (no vertex buffer).
    const vec2 corners[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
    vec2 c = corners[gl_VertexIndex];

    vec2 worldXZ = pc.quadWorldMin + c * pc.quadWorldSize;
    vec2 pageUV = (worldXZ - pc.pageWorldMin) / pc.pageWorldSize; // [0,1] within the page
    gl_Position = vec4(pageUV * 2.0 - 1.0, 0.0, 1.0);

    vWorldXZ = worldXZ;
    vTileUV = (worldXZ - pc.tileWorldMin) / pc.tileWorldSize;
    vTileIndex = pc.fragTileIndex;
}

#type FRAGMENT
#version 460 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl" // TerrainTileGPUData, TerrainLayerGPUData

layout(location = 0) in vec2 vWorldXZ;
layout(location = 1) in vec2 vTileUV;
layout(location = 2) in flat uint vTileIndex;

layout(location = 0) out vec4 outAlbedoEmission;
layout(location = 1) out vec4 outORM;
#ifdef TERRAIN_DETAIL_MAPS
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec4 outEmission;
#endif
#ifdef TERRAIN_RVT_WORLD_HEIGHT
// VK-1620. The world-height plane is always appended LAST, so its MRT location follows the detail
// planes rather than displacing them. The ladder mirrors terrainRVTWorldHeightPlaneIndex() on the
// C++ side; if the two disagree the bake writes height into the emission plane.
#ifdef TERRAIN_DETAIL_MAPS
layout(location = 4) out float outWorldHeight;
#else
layout(location = 2) out float outWorldHeight;
#endif
#endif

// Compact set layout (bake pipeline is independent of the terrain pipeline's 12-set
// layout): set 0 = weightmap+layers, set 1 = bindless, set 2 = tiles. The underlying
// vk::DescriptorSets bound here are the terrain pipeline's own sets.
layout(std430, set = 0, binding = 0) readonly buffer WeightMapBuffer {
    uint weightMapData[];
};
layout(std430, set = 0, binding = 1) readonly buffer TerrainLayerBuffer {
    TerrainLayerGPUData terrainLayers[];
};
// VK-1611: the same weight-map descriptor set the live pipeline binds at set 1 — the bake is
// handed that identical vk::DescriptorSet object — so the shared composite reads the same
// material-global anti-tiling scalars in both paths.
layout(std430, set = 0, binding = 2) readonly buffer TerrainAntiTilingBuffer {
    TerrainAntiTilingGPUData terrainAntiTiling;
};
// VK-1620: per-tile terrain heights, a row-major (res x res) float grid on the SAME grid as the
// splat weights above — so the height sample and the composite sample are co-located by
// construction. Lives on this set because it is the only one the bake pipeline binds.
#ifdef TERRAIN_RVT_WORLD_HEIGHT
layout(std430, set = 0, binding = 3) readonly buffer TerrainHeightBuffer {
    float terrainHeights[];
};
#endif
layout(set = 1, binding = 0) uniform sampler2D bindlessTextures[];
layout(std430, set = 2, binding = 0) readonly buffer TerrainTileBuffer {
    TerrainTileGPUData tiles[];
};

#include "../common/terrain_value_noise.glsl"
#ifdef TERRAIN_HEX_TILING
// Must follow bindlessTextures — the hex helpers index it directly.
#include "../common/hex_tiling_terrain.glsl"
#endif

layout(push_constant) uniform BakePC {
    vec2 pageWorldMin;
    vec2 pageWorldSize;
    vec2 quadWorldMin;
    vec2 quadWorldSize;
    vec2 tileWorldMin;
    float tileWorldSize;
    uint  fragTileIndex;
    float textureScale;
    // VK-1620 — MUST stay identical to the vertex stage's copy of this block above.
    float heightMin;
    float invHeightRange;
    float pad2;
} pc;

// Weight-map helpers — identical to mesh_terrain.glsl (byte-packed 8-channel splat).
float readWeightByte(uint byteOffset) {
    uint wordIndex = byteOffset / 4u;
    uint byteIndex = byteOffset % 4u;
    uint word = weightMapData[wordIndex];
    return float((word >> (byteIndex * 8u)) & 0xFFu) / 255.0;
}
float sampleWeightTexel(uint tileOffset, uint res, uint channel, uint x, uint z) {
    return readWeightByte(tileOffset + (z * res + x) * 8u + channel);
}
float sampleTileWeight(uint tileOffset, uint res, uint channel, vec2 uv) {
    if (res == 0u) return (channel == 0u) ? 1.0 : 0.0;
    uv = clamp(uv, 0.0, 1.0);
    float fx = uv.x * float(res - 1u);
    float fz = uv.y * float(res - 1u);
    uint x0 = uint(floor(fx));
    uint z0 = uint(floor(fz));
    uint x1 = min(x0 + 1u, res - 1u);
    uint z1 = min(z0 + 1u, res - 1u);
    float sx = fract(fx);
    float sz = fract(fz);
    float w00 = sampleWeightTexel(tileOffset, res, channel, x0, z0);
    float w10 = sampleWeightTexel(tileOffset, res, channel, x1, z0);
    float w01 = sampleWeightTexel(tileOffset, res, channel, x0, z1);
    float w11 = sampleWeightTexel(tileOffset, res, channel, x1, z1);
    return mix(mix(w00, w10, sx), mix(w01, w11, sx), sz);
}

#ifdef TERRAIN_RVT_WORLD_HEIGHT
// VK-1620 --- terrain surface height at a tile UV -------------------------------------------
//
// NOT bilinear. TerrainTileGenerator::generateIndices splits every quad on the ANTI-DIAGONAL
// (topLeft, bottomLeft, topRight) + (topRight, bottomLeft, bottomRight), so the rendered surface
// over a quad is two planes hinged on the (x+1,z)-(x,z+1) edge. Bilinear would return
// (h00+h11)/2 down that hinge where the surface gives (h10+h01)/2 — on a ridge that is metres of
// error, and it shows up as props floating above or sinking into the crest they sit on.
//
// Reproducing the split costs one compare and matches the triangle the rasterizer actually drew.
float sampleTileHeight(uint offsetElements, uint res, vec2 uv)
{
    if (res < 2u) return pc.heightMin;
    uv = clamp(uv, 0.0, 1.0);

    // Vertex (x,z) sits at UV (x/(res-1), z/(res-1)) — the texCoords generateVertices writes, and
    // the same mapping sampleTileWeight uses, so both samples land on one grid.
    float gx = uv.x * float(res - 1u);
    float gz = uv.y * float(res - 1u);
    uint x0 = min(uint(floor(gx)), res - 2u);
    uint z0 = min(uint(floor(gz)), res - 2u);
    float fx = gx - float(x0);
    float fz = gz - float(z0);

    uint row0 = offsetElements + z0 * res + x0;
    uint row1 = row0 + res;
    float h00 = terrainHeights[row0];
    float h10 = terrainHeights[row0 + 1u];
    float h01 = terrainHeights[row1];
    float h11 = terrainHeights[row1 + 1u];

    // Lower-left triangle is (0,0),(0,1),(1,0) => the fx + fz <= 1 half.
    return (fx + fz <= 1.0)
        ? h00 + fx * (h10 - h00) + fz * (h01 - h00)
        : h11 + (1.0 - fx) * (h01 - h11) + (1.0 - fz) * (h10 - h11);
}
#endif

void main()
{
    uint fragTileIndex = vTileIndex;
    vec2 fragTexCoord = clamp(vTileUV, 0.0, 1.0);
    vec2 triplanarWorldUV = vWorldXZ * pc.textureScale;
    // VK-1209 finding #7: the composite samples with textureGrad and needs these (here the bake runs in
    // uniform control flow, so the gradients are well-defined either way — kept for a single contract).
    vec2 triplanarWorldUVdx = dFdx(triplanarWorldUV);
    vec2 triplanarWorldUVdy = dFdy(triplanarWorldUV);

    // VK-1611 composite contract, the bake's half.
    // terrainWorldXZ is unscaled world XZ, so macro variation is anchored identically here and in
    // the live path — that is what makes it view-independent and therefore safe to bake.
    vec2 terrainWorldXZ = vWorldXZ;
    // Same footprint formula as mesh_terrain.glsl. There is no camera in this pass; the value it
    // produces is the PAGE's texel density, which rises with the page's virtual-texture mip. That
    // is precisely the quantity the live path's distance rescale keys off, because VT residency
    // picks the page mip so a page texel is about a screen pixel — so bake and live agree by
    // construction rather than by tuning. (Epic solves the same problem the same way: RVT shading
    // is camera-independent, so distance effects are expressed as mip-level-dependent shading.)
    float terrainFootprintLog2 = 0.5 * log2(max(max(dot(triplanarWorldUVdx, triplanarWorldUVdx),
                                                    dot(triplanarWorldUVdy, triplanarWorldUVdy)), 1e-30));

    // The composite declares the material properties and legacy scalar emission accumulator.
#include "../material/terrain_material_generated.glsl"

    float emissionStrength = clamp(ls_Emission, 0.0, 1.0);
    outAlbedoEmission = vec4(mat_albedo, emissionStrength);
    // ORM alpha = per-texel "baked with real content" bit (VK-1209). Every rasterized texel
    // is covered by loaded terrain, so it writes 1.0; the per-page clear / pool seed leave
    // uncovered texels at 0.0. mesh_terrain.glsl reads this to decide RVT vs composite fallback.
    outORM = vec4(mat_ao, mat_roughness, mat_metallic, 1.0);
#ifdef TERRAIN_DETAIL_MAPS
    outNormal = vec4(mat_normalTS * 0.5 + 0.5, 1.0);
    // The detail-map RVT uses a floating-point plane; preserve emission above 1.0.
    outEmission = vec4(mat_emission, 1.0);
#endif
#ifdef TERRAIN_RVT_WORLD_HEIGHT
    // caveMeshletData.w is heightFieldOffset + 1, so 0 means this tile has no height data
    // (never uploaded, or the arena was full). Writing heightMin there puts the surface at the
    // bottom of the range, which reads as "terrain far below" and makes props NOT blend — the
    // safe failure. aabbMin.w is the weight-map resolution, which is also the height grid's.
    uint heightSlot = tiles[fragTileIndex].caveMeshletData.w;
    float worldY = (heightSlot != 0u)
        ? sampleTileHeight(heightSlot - 1u, uint(tiles[fragTileIndex].aabbMin.w), fragTexCoord)
        : pc.heightMin;
    outWorldHeight = clamp((worldY - pc.heightMin) * pc.invHeightRange, 0.0, 1.0);
#endif
}
