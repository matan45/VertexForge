#type COMPUTE
#version 460 core
#extension GL_GOOGLE_include_directive : require
// Cloud temporal reprojection compute shader
// Blends current frame cloud result with reprojected history

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) uniform image2D currentResult;  // read-write
layout(set = 0, binding = 1, rgba16f) uniform image2D historyResult;  // read-write (becomes output)

layout(set = 0, binding = 2) uniform CloudTemporalParams {
    mat4 invViewProjection;
    mat4 prevViewProjection;
    vec4 screenParams;      // x=halfWidth, y=halfHeight, z=near, w=far
    vec4 temporalParams;    // x=blendFactor, y=frameIndex, z=0, w=0
} params;

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 halfSize = ivec2(params.screenParams.x, params.screenParams.y);
    if (any(greaterThanEqual(texel, halfSize)))
        return;

    vec4 current = imageLoad(currentResult, texel);

    // Reproject: find where this pixel was in previous frame
    vec2 uv = (vec2(texel) + 0.5) / vec2(halfSize);
    vec4 clipPos = vec4(uv * 2.0 - 1.0, 0.5, 1.0);
    vec4 worldPos = params.invViewProjection * clipPos;
    worldPos /= worldPos.w;

    vec4 prevClip = params.prevViewProjection * worldPos;
    vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // Sample history with bilinear interpolation (manual, since we use imageLoad)
    vec2 histCoord = prevUV * vec2(halfSize) - 0.5;
    ivec2 histTexel = ivec2(floor(histCoord));
    vec2 frac = histCoord - vec2(histTexel);

    vec4 history = vec4(0.0, 0.0, 0.0, 1.0);
    bool validReproject = prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0
                          && prevClip.w > 0.0;

    if (validReproject)
    {
        // Bilinear sample from history
        ivec2 c00 = clamp(histTexel, ivec2(0), halfSize - 1);
        ivec2 c10 = clamp(histTexel + ivec2(1, 0), ivec2(0), halfSize - 1);
        ivec2 c01 = clamp(histTexel + ivec2(0, 1), ivec2(0), halfSize - 1);
        ivec2 c11 = clamp(histTexel + ivec2(1, 1), ivec2(0), halfSize - 1);

        vec4 s00 = imageLoad(historyResult, c00);
        vec4 s10 = imageLoad(historyResult, c10);
        vec4 s01 = imageLoad(historyResult, c01);
        vec4 s11 = imageLoad(historyResult, c11);

        history = mix(mix(s00, s10, frac.x), mix(s01, s11, frac.x), frac.y);
    }

    // Neighbourhood clamping to reduce ghosting
    vec4 minVal = current;
    vec4 maxVal = current;
    for (int dx = -1; dx <= 1; ++dx)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            ivec2 neighbor = clamp(texel + ivec2(dx, dy), ivec2(0), halfSize - 1);
            vec4 n = imageLoad(currentResult, neighbor);
            minVal = min(minVal, n);
            maxVal = max(maxVal, n);
        }
    }

    if (validReproject)
    {
        // AABB clip-towards-center (preserves more temporal information than hard clamp)
        vec4 center = (minVal + maxVal) * 0.5;
        vec4 halfExtent = (maxVal - minVal) * 0.5 + 0.001;
        vec4 offset = history - center;
        vec4 ts = abs(offset / halfExtent);
        float maxScale = max(max(ts.r, ts.g), max(ts.b, ts.a));
        if (maxScale > 1.0)
            history = center + offset / maxScale;
    }

    // Adaptive blend: reduce blend when reprojection confidence is low
    float blendFactor = validReproject ? params.temporalParams.x : 0.0;
    if (validReproject)
    {
        // Transmittance-based disocclusion detection
        float transmittanceDiff = abs(history.a - current.a);
        float confidence = 1.0 - smoothstep(0.05, 0.3, transmittanceDiff);

        // Reduce blend in high-variance regions (cloud edges)
        vec4 variance = maxVal - minVal;
        float varMag = dot(variance.rgb, vec3(0.333));
        blendFactor *= confidence * mix(0.3, 1.0, exp(-varMag * 5.0));
    }

    vec4 result = mix(current, history, blendFactor);

    // Write blended result back to history (ping-pong)
    imageStore(historyResult, texel, result);
}
