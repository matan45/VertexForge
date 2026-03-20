#ifndef HIZ_OCCLUSION_GLSL
#define HIZ_OCCLUSION_GLSL

// Shared Hi-Z occlusion test used by:
// - gpu_cull_lod.glsl (object-level compute culling)
// - task_gpudriven.glsl (meshlet-level scene culling)
// - task_terrain.glsl (tile + meshlet-level terrain culling)
//
// Requires: hiZTexture (sampler2D) bound in the including shader.

bool hiZOcclusionTest(sampler2D hiZTex, vec4 worldSphere, mat4 viewProjection, vec2 screenSize, uint hiZMipLevels) {
    vec3 center = worldSphere.xyz;
    float radius = worldSphere.w;
    vec3 aabbMin = center - vec3(radius);
    vec3 aabbMax = center + vec3(radius);

    vec4 corners[8];
    corners[0] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0);
    corners[1] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0);
    corners[2] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0);
    corners[3] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0);
    corners[4] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0);
    corners[5] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0);
    corners[6] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0);
    corners[7] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0);

    vec2 ndcMin = vec2(1.0);
    vec2 ndcMax = vec2(-1.0);
    float minDepth = 1.0;

    for (int i = 0; i < 8; i++) {
        if (corners[i].w <= 0.0) {
            return true; // Behind camera, assume visible
        }
        vec3 ndc = corners[i].xyz / corners[i].w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        minDepth = min(minDepth, ndc.z);
    }

    ndcMin = clamp(ndcMin, vec2(-1.0), vec2(1.0));
    ndcMax = clamp(ndcMax, vec2(-1.0), vec2(1.0));

    if (minDepth < 0.0) {
        return true; // Intersects near plane, assume visible
    }

    vec2 uvMin = ndcMin * 0.5 + 0.5;
    vec2 uvMax = ndcMax * 0.5 + 0.5;
    vec2 sizePixels = (uvMax - uvMin) * screenSize;
    float maxDimension = max(sizePixels.x, sizePixels.y);
    float mipLevel = ceil(log2(maxDimension));
    mipLevel = clamp(mipLevel, 0.0, float(hiZMipLevels - 1u));

    float hiZDepth = 0.0;
    hiZDepth = max(hiZDepth, textureLod(hiZTex, uvMin, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTex, uvMax, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTex, vec2(uvMin.x, uvMax.y), mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTex, vec2(uvMax.x, uvMin.y), mipLevel).r);

    return minDepth <= hiZDepth + 0.0001;
}

// AABB variant for tile-level occlusion testing
bool hiZOcclusionTestAABB(sampler2D hiZTex, vec3 aabbMin, vec3 aabbMax, mat4 viewProjection, vec2 screenSize, uint hiZMipLevels) {
    vec4 corners[8];
    corners[0] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0);
    corners[1] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0);
    corners[2] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0);
    corners[3] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0);
    corners[4] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0);
    corners[5] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0);
    corners[6] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0);
    corners[7] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0);

    vec2 ndcMin = vec2(1.0);
    vec2 ndcMax = vec2(-1.0);
    float minDepth = 1.0;

    for (int i = 0; i < 8; i++) {
        if (corners[i].w <= 0.0) {
            return true;
        }
        vec3 ndc = corners[i].xyz / corners[i].w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        minDepth = min(minDepth, ndc.z);
    }

    ndcMin = clamp(ndcMin, vec2(-1.0), vec2(1.0));
    ndcMax = clamp(ndcMax, vec2(-1.0), vec2(1.0));

    if (minDepth < 0.0) {
        return true;
    }

    vec2 uvMin = ndcMin * 0.5 + 0.5;
    vec2 uvMax = ndcMax * 0.5 + 0.5;
    vec2 sizePixels = (uvMax - uvMin) * screenSize;
    float maxDimension = max(sizePixels.x, sizePixels.y);
    float mipLevel = ceil(log2(maxDimension));
    mipLevel = clamp(mipLevel, 0.0, float(hiZMipLevels - 1u));

    float hiZDepth = 0.0;
    hiZDepth = max(hiZDepth, textureLod(hiZTex, uvMin, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTex, uvMax, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTex, vec2(uvMin.x, uvMax.y), mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTex, vec2(uvMax.x, uvMin.y), mipLevel).r);

    return minDepth <= hiZDepth + 0.0001;
}

#endif // HIZ_OCCLUSION_GLSL
