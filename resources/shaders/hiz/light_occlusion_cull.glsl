#type COMPUTE
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D hiZPyramid;

struct LightBounds {
    vec4 positionRadius;
    vec4 direction;
    uint entityId;
    uint lightType;
    uint padding0;
    uint padding1;
};

layout(std430, set = 0, binding = 1) readonly buffer LightBoundsBuffer {
    LightBounds lights[];
};

layout(std430, set = 0, binding = 2) writeonly buffer VisibilityBuffer {
    uint visibility[];
};

layout(set = 0, binding = 3) uniform CameraUBO {
    mat4 viewProj;
    vec4 screenSize;
    vec4 cameraPos;
    uint lightCount;
    uint hiZMipLevels;
    uint padding0;
    uint padding1;
};

bool testSphereVisible(vec3 center, float radius) {
    vec3 aabbMin = center - vec3(radius);
    vec3 aabbMax = center + vec3(radius);

    vec4 corners[8];
    corners[0] = viewProj * vec4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0);
    corners[1] = viewProj * vec4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0);
    corners[2] = viewProj * vec4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0);
    corners[3] = viewProj * vec4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0);
    corners[4] = viewProj * vec4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0);
    corners[5] = viewProj * vec4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0);
    corners[6] = viewProj * vec4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0);
    corners[7] = viewProj * vec4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0);

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

    vec2 sizePixels = (uvMax - uvMin) * screenSize.xy;
    float maxDimension = max(sizePixels.x, sizePixels.y);

    if (maxDimension < 1.0) {
        return true;
    }

    float mipLevel = ceil(log2(maxDimension));
    mipLevel = clamp(mipLevel, 0.0, float(hiZMipLevels - 1));

    float hiZDepth = 0.0;
    hiZDepth = max(hiZDepth, textureLod(hiZPyramid, uvMin, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZPyramid, uvMax, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZPyramid, vec2(uvMin.x, uvMax.y), mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZPyramid, vec2(uvMax.x, uvMin.y), mipLevel).r);

    return minDepth <= hiZDepth + 0.0001;
}

void main() {
    uint lightIndex = gl_GlobalInvocationID.x;

    if (lightIndex >= lightCount) {
        return;
    }

    LightBounds light = lights[lightIndex];

    vec3 position = light.positionRadius.xyz;
    float radius = light.positionRadius.w;

    bool isVisible = testSphereVisible(position, radius);

    visibility[lightIndex] = isVisible ? 1u : 0u;
}
