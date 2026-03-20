#ifndef HIZ_OCCLUSION_GLSL
#define HIZ_OCCLUSION_GLSL

bool sampleHiZFromAABB(sampler2D hiZTex, vec3 aabbMin, vec3 aabbMax,
                        mat4 viewProjection, vec2 screenSize, uint hiZMipLevels) {
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
        if (corners[i].w <= 0.0) return true;
        vec3 ndc = corners[i].xyz / corners[i].w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        minDepth = min(minDepth, ndc.z);
    }

    ndcMin = clamp(ndcMin, vec2(-1.0), vec2(1.0));
    ndcMax = clamp(ndcMax, vec2(-1.0), vec2(1.0));

    if (minDepth < 0.0) return true;

    vec2 uvMin = ndcMin * 0.5 + 0.5;
    vec2 uvMax = ndcMax * 0.5 + 0.5;
    vec2 sizePixels = (uvMax - uvMin) * screenSize;
    float maxDimension = max(sizePixels.x, sizePixels.y);
    float mipLevel = clamp(ceil(log2(maxDimension)), 0.0, float(hiZMipLevels - 1u));

    float hiZDepth = max(
        max(textureLod(hiZTex, uvMin, mipLevel).r, textureLod(hiZTex, uvMax, mipLevel).r),
        max(textureLod(hiZTex, vec2(uvMin.x, uvMax.y), mipLevel).r,
            textureLod(hiZTex, vec2(uvMax.x, uvMin.y), mipLevel).r));

    return minDepth <= hiZDepth + 0.0001;
}

bool hiZOcclusionTest(sampler2D hiZTex, vec4 worldSphere, mat4 viewProjection, vec2 screenSize, uint hiZMipLevels) {
    vec3 aabbMin = worldSphere.xyz - vec3(worldSphere.w);
    vec3 aabbMax = worldSphere.xyz + vec3(worldSphere.w);
    return sampleHiZFromAABB(hiZTex, aabbMin, aabbMax, viewProjection, screenSize, hiZMipLevels);
}

bool hiZOcclusionTestAABB(sampler2D hiZTex, vec3 aabbMin, vec3 aabbMax, mat4 viewProjection, vec2 screenSize, uint hiZMipLevels) {
    return sampleHiZFromAABB(hiZTex, aabbMin, aabbMax, viewProjection, screenSize, hiZMipLevels);
}

#endif // HIZ_OCCLUSION_GLSL
