// VK-1209 — terrain Runtime Virtual Texture bake shader. Renders one quad per
// (page, overlapping terrain tile) with an orthographic mapping of the page's world
// XZ rect into the page's atlas tile, and writes the SAME terrain splat composite
// (terrain_material_generated.glsl, included verbatim) into 2 RGBA8 MRT planes:
//   plane 0 = albedo.rgb + emissionStrength.a
//   plane 1 = ao.r + roughness.g + metallic.b
// The main terrain pass then samples these 2 texels instead of compositing 8 layers
// (up to 24 bindless samples) per fragment. Bake triplanar uses the XZ projection only
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
    float pad0; float pad1; float pad2;
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

// Compact set layout (bake pipeline is independent of the terrain pipeline's 12-set
// layout): set 0 = weightmap+layers, set 1 = bindless, set 2 = tiles. The underlying
// vk::DescriptorSets bound here are the terrain pipeline's own sets.
layout(std430, set = 0, binding = 0) readonly buffer WeightMapBuffer {
    uint weightMapData[];
};
layout(std430, set = 0, binding = 1) readonly buffer TerrainLayerBuffer {
    TerrainLayerGPUData terrainLayers[];
};
layout(set = 1, binding = 0) uniform sampler2D bindlessTextures[];
layout(std430, set = 2, binding = 0) readonly buffer TerrainTileBuffer {
    TerrainTileGPUData tiles[];
};

layout(push_constant) uniform BakePC {
    vec2 pageWorldMin;
    vec2 pageWorldSize;
    vec2 quadWorldMin;
    vec2 quadWorldSize;
    vec2 tileWorldMin;
    float tileWorldSize;
    uint  fragTileIndex;
    float textureScale;
    float pad0; float pad1; float pad2;
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

void main()
{
    uint fragTileIndex = vTileIndex;
    vec2 fragTexCoord = clamp(vTileUV, 0.0, 1.0);
    vec2 triplanarWorldUV = vWorldXZ * pc.textureScale;

    // The composite declares mat_albedo/normalTS/metallic/roughness/ao/emission + ls_Emission.
#include "../material/terrain_material_generated.glsl"

    float emissionStrength = clamp(ls_Emission, 0.0, 1.0);
    outAlbedoEmission = vec4(mat_albedo, emissionStrength);
    outORM = vec4(mat_ao, mat_roughness, mat_metallic, 0.0);
}
