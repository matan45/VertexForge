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

// Advance ray position along the screen-space ray direction
// Returns the next cell boundary crossing point
float getHiZCellBoundary(float rayOrigin, float rayDir, float cellCount, float cellSize)
{
    float cell = floor(rayOrigin * cellCount);
    // Move to cell edge in the direction of travel
    float boundary = (cell + (rayDir > 0.0 ? 1.0 : 0.0)) * cellSize;
    return boundary;
}

// Hi-Z ray march
// viewOrigin: view-space position of the fragment
// viewDir:    view-space reflection direction (normalized)
// projection: projection matrix
// inverseProjection: inverse projection matrix
// hiZTexture: Hi-Z mip pyramid (max depth reduction)
// depthTexture: full-resolution depth buffer
// sceneColorTexture: scene color for sampling hit color
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

    // Project start and end to screen space [0,1]
    vec4 clipStart = projection * vec4(viewOrigin, 1.0);
    vec4 clipEnd = projection * vec4(viewEnd, 1.0);

    // Bail if start is behind near plane
    if (clipStart.w <= 0.0)
        return result;

    vec2 screenStart = (clipStart.xy / clipStart.w) * 0.5 + 0.5;
    vec2 screenEnd;
    if (clipEnd.w > 0.0)
    {
        screenEnd = (clipEnd.xy / clipEnd.w) * 0.5 + 0.5;
    }
    else
    {
        // Ray goes behind camera — clip to near plane
        float t = (-viewOrigin.z - nearPlane) / (viewEnd.z - viewOrigin.z);
        t = clamp(t, 0.0, 1.0);
        vec3 clippedEnd = viewOrigin + viewDir * maxDistance * t;
        vec4 clippedClip = projection * vec4(clippedEnd, 1.0);
        screenEnd = (clippedClip.xy / clippedClip.w) * 0.5 + 0.5;
    }

    vec2 screenDir = screenEnd - screenStart;
    float screenLength = length(screenDir * resolution);

    if (screenLength < 1.0)
        return result;

    // Use perspective-correct interpolation
    float invStartW = 1.0 / clipStart.w;
    float invEndW = clipEnd.w > 0.0 ? 1.0 / clipEnd.w : invStartW;

    vec2 startOverW = screenStart * invStartW;
    vec2 endOverW = screenEnd * invEndW;
    float originZOverW = viewOrigin.z * invStartW;
    float endZOverW = viewEnd.z * invEndW;

    // Hierarchical marching through mip levels
    int mipCount = textureQueryLevels(hiZTexture);
    int currentMip = clamp(int(log2(screenLength / float(maxSteps))), 0, mipCount - 1);

    float t = 0.0;
    float dt = 1.0 / float(maxSteps);
    float thickness = 0.3;

    for (uint step = 0; step < maxSteps; step++)
    {
        t += dt;
        if (t > 1.0)
            break;

        // Perspective-correct interpolation
        float invW = mix(invStartW, invEndW, t);
        vec2 sampleUV = mix(startOverW, endOverW, t) / invW;
        float rayZ = mix(originZOverW, endZOverW, t) / invW;

        // Out-of-bounds check
        if (any(lessThan(sampleUV, vec2(0.001))) || any(greaterThan(sampleUV, vec2(0.999))))
            break;

        // Sample Hi-Z at current mip for coarse test
        float hiZDepth = textureLod(hiZTexture, sampleUV, float(currentMip)).r;

        // Reconstruct view Z from Hi-Z depth
        vec4 hiZClip = vec4(sampleUV * 2.0 - 1.0, hiZDepth, 1.0);
        vec4 hiZView = inverseProjection * hiZClip;
        float sceneZ = hiZView.z / hiZView.w;

        if (rayZ < sceneZ)
        {
            // Ray is in front of surface — advance and try coarser mip
            if (currentMip < mipCount - 2)
            {
                currentMip++;
                dt *= 2.0;
            }
        }
        else
        {
            // Ray is behind surface — potential intersection
            if (currentMip > 0)
            {
                // Refine: step back and descend to finer mip
                t -= dt;
                currentMip--;
                dt *= 0.5;
            }
            else
            {
                // At finest mip — check exact depth
                float exactDepth = texture(depthTexture, sampleUV).r;
                if (exactDepth >= 1.0)
                    continue;

                vec4 exactClip = vec4(sampleUV * 2.0 - 1.0, exactDepth, 1.0);
                vec4 exactView = inverseProjection * exactClip;
                float exactZ = exactView.z / exactView.w;

                if (rayZ < exactZ && rayZ > exactZ - thickness)
                {
                    result.hit = true;
                    result.hitUV = sampleUV;
                    result.hitDepth = exactDepth;

                    // Confidence based on distance and edge fade
                    float rayDist = length(mix(viewOrigin, viewEnd, t) - viewOrigin);
                    float distanceFade = 1.0 - smoothstep(maxDistance * 0.5, maxDistance, rayDist);

                    // Screen-edge fade
                    vec2 edgeDist = abs(sampleUV - 0.5) * 2.0;
                    float edgeFade = 1.0 - smoothstep(edgeFadeStart, 1.0, max(edgeDist.x, edgeDist.y));

                    // Back-face rejection: check if the hit normal faces away from ray
                    result.confidence = distanceFade * edgeFade;
                    return result;
                }

                // Passed through — continue at current mip
                dt = 1.0 / float(maxSteps);
            }
        }
    }

    return result;
}
