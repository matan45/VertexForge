// Hierarchical Hi-Z ray marching for SSR
// Marches a reflection ray through the Hi-Z depth pyramid for efficient
// screen-space intersection testing.

struct HiZRayResult
{
    vec2 hitUV;
    float hitDepth;
    float confidence;
    bool hit;
};

// Hi-Z ray march using linear screen-space stepping with Hi-Z acceleration
// viewOrigin: view-space position of the fragment
// viewDir:    view-space reflection direction (normalized)
// projection: projection matrix
// inverseProjection: inverse projection matrix
// hiZTexture: Hi-Z mip pyramid (max depth reduction)
// depthTexture: full-resolution depth buffer
// resolution: trace resolution
// nearPlane, farPlane: camera planes
// maxSteps: maximum march steps
// maxDistance: maximum ray distance in view space
// edgeFadeStart: screen-edge fade threshold (0-1 from center)
HiZRayResult hiZRayMarch(
    vec3 viewOrigin,
    vec3 viewDir,
    mat4 projection,
    mat4 inverseProjection,
    sampler2D hiZTexture,
    sampler2D depthTexture,
    vec2 resolution,
    float nearPlane,
    float farPlane,
    uint maxSteps,
    float maxDistance,
    float edgeFadeStart)
{
    HiZRayResult result;
    result.hit = false;
    result.hitUV = vec2(0.0);
    result.hitDepth = 0.0;
    result.confidence = 0.0;

    // Compute ray end point in view space
    vec3 viewEnd = viewOrigin + viewDir * maxDistance;

    // Project start and end to clip space
    vec4 clipStart = projection * vec4(viewOrigin, 1.0);
    vec4 clipEnd = projection * vec4(viewEnd, 1.0);

    // Bail if start is behind near plane
    if (clipStart.w <= 0.0)
        return result;

    // Screen-space coordinates [0,1]
    vec2 screenStart = (clipStart.xy / clipStart.w) * 0.5 + 0.5;

    // Clamp end point if it goes behind camera
    float tClip = 1.0;
    if (clipEnd.w <= 0.0)
    {
        // Find t where the ray reaches near plane in view space
        // viewOrigin.z + viewDir.z * t * maxDistance = -nearPlane
        float tNear = (-nearPlane - viewOrigin.z) / (viewDir.z * maxDistance);
        tClip = clamp(tNear * 0.95, 0.01, 1.0);
        viewEnd = viewOrigin + viewDir * maxDistance * tClip;
        clipEnd = projection * vec4(viewEnd, 1.0);
    }

    vec2 screenEnd = (clipEnd.xy / clipEnd.w) * 0.5 + 0.5;

    // Screen-space ray direction and length
    vec2 screenDir = screenEnd - screenStart;
    float screenDist = length(screenDir * resolution);

    if (screenDist < 1.0)
        return result;

    // Use perspective-correct interpolation
    float invStartW = 1.0 / clipStart.w;
    float invEndW = 1.0 / clipEnd.w;
    vec2 startOverW = screenStart * invStartW;
    vec2 endOverW = screenEnd * invEndW;
    float startDepth = clipStart.z / clipStart.w; // NDC depth at start
    float endDepth = clipEnd.z / clipEnd.w;       // NDC depth at end

    // Linear march in screen space with depth comparison
    float thickness = 0.05; // NDC depth thickness for hit detection

    int stepCount = int(min(float(maxSteps), screenDist));
    stepCount = max(stepCount, 16);

    for (int step = 1; step <= stepCount; step++)
    {
        float t = float(step) / float(stepCount);

        // Perspective-correct interpolation of UV and depth
        float invW = mix(invStartW, invEndW, t);
        vec2 sampleUV = mix(startOverW, endOverW, t) / invW;
        float rayDepth = mix(startDepth * invStartW, endDepth * invEndW, t) / invW;

        // Out-of-bounds check
        if (any(lessThan(sampleUV, vec2(0.002))) || any(greaterThan(sampleUV, vec2(0.998))))
            break;

        // Use Hi-Z for early skip at coarser mips first
        int mipLevel = clamp(int(log2(float(stepCount) / float(step + 1))), 0, 4);
        float hiZDepth = textureLod(hiZTexture, sampleUV, float(mipLevel)).r;

        // If ray depth is less than Hi-Z max depth at this mip, no intersection possible here
        // (Hi-Z stores max depth — if ray is closer than max, it might be in front of everything)
        if (rayDepth < hiZDepth && mipLevel > 0)
            continue;

        // Sample exact depth at mip 0
        float sceneDepth = texture(depthTexture, sampleUV).r;

        // Skip sky pixels
        if (sceneDepth >= 1.0)
            continue;

        // Check intersection: ray depth is behind scene depth (greater depth = farther)
        float depthDiff = rayDepth - sceneDepth;

        if (depthDiff > 0.0 && depthDiff < thickness)
        {
            result.hit = true;
            result.hitUV = sampleUV;
            result.hitDepth = sceneDepth;

            // Confidence based on distance and screen-edge fade
            float rayT = t * tClip;
            float distanceFade = 1.0 - smoothstep(maxDistance * 0.7, maxDistance, rayT * maxDistance);

            // Screen-edge fade
            vec2 edgeDist = abs(sampleUV - 0.5) * 2.0;
            float edgeFade = 1.0 - smoothstep(edgeFadeStart, 1.0, max(edgeDist.x, edgeDist.y));

            result.confidence = distanceFade * edgeFade;
            return result;
        }
    }

    return result;
}
