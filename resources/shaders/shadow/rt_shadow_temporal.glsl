#type COMPUTE
#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Set 0: All bindings
layout(set = 0, binding = 0, r8)   uniform readonly  image2D rawShadow;
layout(set = 0, binding = 1, r16f) uniform readonly  image2D historyRead;
layout(set = 0, binding = 2, r16f) uniform writeonly image2D historyWrite;
layout(set = 0, binding = 3) uniform sampler2D depthBuffer;
layout(set = 0, binding = 4) uniform sampler2D normalBuffer;

layout(std140, set = 0, binding = 5) uniform DenoiserParams {
    mat4 invViewProjection;
    mat4 prevViewProjection;
    vec4 screenParams;      // xy = resolution, zw = 1/resolution
    vec4 temporalParams;    // x = blend, y = frameIndex, z = depthThreshold, w = normalThreshold
};

vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = invViewProjection * clip;
    return world.xyz / world.w;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenSize = ivec2(screenParams.xy);

    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y)
        return;

    vec2 uv = (vec2(pixel) + 0.5) * screenParams.zw;
    float currentShadow = imageLoad(rawShadow, pixel).r;
    float depth = texture(depthBuffer, uv).r;

    // Sky: pass through
    if (depth >= 1.0 || depth <= 0.0) {
        imageStore(historyWrite, pixel, vec4(currentShadow));
        return;
    }

    float blendFactor = temporalParams.x;
    float depthThreshold = temporalParams.z;
    float normalThreshold = temporalParams.w;

    // Reproject to previous frame
    vec3 worldPos = reconstructWorldPos(uv, depth);
    vec4 prevClip = prevViewProjection * vec4(worldPos, 1.0);
    vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    bool validReproject = blendFactor > 0.0 &&
                          prevClip.w > 0.0 &&
                          all(greaterThanEqual(prevUV, vec2(0.0))) &&
                          all(lessThanEqual(prevUV, vec2(1.0)));

    float historySample = currentShadow;

    if (validReproject) {
        // Bilinear sample from history via 4-tap imageLoad
        vec2 histCoord = prevUV * vec2(screenSize) - 0.5;
        ivec2 histTexel = ivec2(floor(histCoord));
        vec2 f = histCoord - vec2(histTexel);

        float s00 = imageLoad(historyRead, clamp(histTexel,              ivec2(0), screenSize - 1)).r;
        float s10 = imageLoad(historyRead, clamp(histTexel + ivec2(1,0), ivec2(0), screenSize - 1)).r;
        float s01 = imageLoad(historyRead, clamp(histTexel + ivec2(0,1), ivec2(0), screenSize - 1)).r;
        float s11 = imageLoad(historyRead, clamp(histTexel + ivec2(1,1), ivec2(0), screenSize - 1)).r;
        historySample = mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);

        // Disocclusion detection: depth and normal consistency
        vec3 currentNormal = normalize(texture(normalBuffer, uv).xyz);
        vec3 prevNormal = normalize(texture(normalBuffer, prevUV).xyz);
        float prevDepth = texture(depthBuffer, prevUV).r;

        float depthDiff = abs(depth - prevDepth);
        float normalDot = max(dot(currentNormal, prevNormal), 0.0);

        if (depthDiff > depthThreshold || normalDot < normalThreshold) {
            validReproject = false;
        }
    }

    // 3x3 neighbourhood clamp to prevent ghosting
    float minVal = currentShadow;
    float maxVal = currentShadow;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            float n = imageLoad(rawShadow, clamp(pixel + ivec2(dx, dy), ivec2(0), screenSize - 1)).r;
            minVal = min(minVal, n);
            maxVal = max(maxVal, n);
        }
    }
    historySample = clamp(historySample, minVal, maxVal);

    float blend = validReproject ? blendFactor : 0.0;
    float result = mix(currentShadow, historySample, blend);

    imageStore(historyWrite, pixel, vec4(result));
}
