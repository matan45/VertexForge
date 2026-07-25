#ifndef WATER_SSR_GLSL
#define WATER_SSR_GLSL

// VK-1604: in-fragment screen-space reflections for the water surface.
//
// Why not reuse the engine's SSR chain (resources/shaders/ssr/*): it runs in the PostScene
// segment off the depth-prepass normal-roughness buffer, and water is not in the depth prepass —
// it draws afterwards with depthWriteEnable = false, leaving neither normals nor depth for that
// pass to trace from. So the march has to happen here, in the water fragment shader (the same
// approach UE5 takes for Single Layer Water).
//
// Why not #include common/hiz_ray_march.glsl: it needs a Hi-Z pyramid (water binds none), it has
// no binary-search refinement, and its thickness test is a hardcoded 0.05 in NDC — which is
// centimetres near the camera and hundreds of metres at range, i.e. it fails exactly at the
// grazing distances where water reflections matter. Water is near-mirror (roughness 0.05), so
// unrefined steps stair-step visibly.
//
// Requires, all already bound to the water fragment stage:
//   camera        (set 0 b0)   - view, projection
//   clusterParams (set 4 b0)   - invProjection, depthParams.xy = near/far, screenParams.xy
//   refractionColorTex, sceneDepthTex (set 9 b0/b1)
//   ext           (set 9 b2)   - ssr* tuning
//
// Documented limitation (same class as UE5 SLW): the traced colour is the PRE-water scene copy,
// so reflections exclude transparents, VFX and other water surfaces. The IBL cubemap covers
// misses, screen edges and the sky.

struct WaterSSRResult {
    vec3 color;
    float confidence;   // 0 = use IBL only
};

// View-space position of a screen pixel from its non-linear depth.
vec3 waterSSRViewPosFromDepth(vec2 uv, float rawDepth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, rawDepth, 1.0);
    vec4 view = clusterParams.invProjection * clip;
    return view.xyz / view.w;
}

WaterSSRResult traceWaterSSR(vec3 fragViewPos, vec3 reflectDirView)
{
    WaterSSRResult result;
    result.color = vec3(0.0);
    result.confidence = 0.0;

    // Rays pointing back toward the camera skim the screen and produce long smears.
    if (reflectDirView.z > -0.02)
        return result;

    uint maxSteps = max(ext.ssrMaxSteps, 1u);
    float stride = ext.ssrMaxDistance / float(maxSteps);

    float travelled = 0.0;
    float prevTravelled = 0.0;
    bool hit = false;
    vec2 hitUV = vec2(0.0);
    float hitThickness = 0.0;

    for (uint i = 0u; i < maxSteps; ++i)
    {
        prevTravelled = travelled;
        travelled += stride;
        // Grow the step geometrically: near the surface we want precision, far away we want reach.
        stride *= 1.05;

        vec3 samplePos = fragViewPos + reflectDirView * travelled;
        vec4 clip = camera.projection * vec4(samplePos, 1.0);
        if (clip.w <= 0.0)
            break;

        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            break;

        float rawDepth = texture(sceneDepthTex, uv).r;
        // Sky: nothing to reflect off, but geometry may still lie further along the ray.
        if (rawDepth >= 1.0)
            continue;

        float sceneZ = linearizeDepth(clusterParams, rawDepth);
        float rayZ = -samplePos.z;

        // Thickness in LINEAR metres, scaled with range: both depth precision and step length
        // grow with distance, so a fixed thickness misses distant geometry.
        float thickness = max(ext.ssrThickness, sceneZ * 0.02);

        float diff = rayZ - sceneZ;
        if (diff > 0.0 && diff < thickness)
        {
            hit = true;
            hitUV = uv;
            hitThickness = diff / thickness;
            break;
        }
    }

    if (!hit)
        return result;

    // Binary-search refine between the last miss and the hit. Without this, a near-mirror
    // surface shows the marching step size as stair-stepping along every reflected edge.
    float lo = prevTravelled;
    float hi = travelled;
    uint refineSteps = uint(max(ext.ssrRefineSteps, 0.0));
    for (uint i = 0u; i < refineSteps; ++i)
    {
        float mid = 0.5 * (lo + hi);
        vec3 samplePos = fragViewPos + reflectDirView * mid;
        vec4 clip = camera.projection * vec4(samplePos, 1.0);
        if (clip.w <= 0.0)
            break;

        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            break;

        float rawDepth = texture(sceneDepthTex, uv).r;
        if (rawDepth >= 1.0)
        {
            lo = mid;
            continue;
        }

        float sceneZ = linearizeDepth(clusterParams, rawDepth);
        float rayZ = -samplePos.z;

        if (rayZ > sceneZ)
        {
            hi = mid;
            hitUV = uv;
            hitThickness = (rayZ - sceneZ) / max(ext.ssrThickness, sceneZ * 0.02);
        }
        else
        {
            lo = mid;
        }
    }

    // Confidence: every term in [0,1], multiplied together.
    vec2 edgeDist = abs(hitUV * 2.0 - 1.0);
    float edgeFade = 1.0 - smoothstep(ext.ssrEdgeFadeStart, 1.0, max(edgeDist.x, edgeDist.y));
    float distanceFade = 1.0 - smoothstep(ext.ssrMaxDistance * 0.7, ext.ssrMaxDistance, hi);
    float grazingFade = smoothstep(0.05, 0.25, -reflectDirView.z);
    float thicknessFade = 1.0 - clamp(hitThickness, 0.0, 1.0);

    result.color = texture(refractionColorTex, hitUV).rgb;
    result.confidence = clamp(edgeFade * distanceFade * grazingFade * thicknessFade
                              * ext.ssrIntensity, 0.0, 1.0);
    return result;
}

#endif // WATER_SSR_GLSL
