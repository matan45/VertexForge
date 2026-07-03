#ifndef RT_SHADOW_TRACE_COMMON_GLSL
#define RT_SHADOW_TRACE_COMMON_GLSL

// Shared ray-query shadow trace body for the RT shadow compute shaders
// (rt_shadow.glsl single-mask directional path, rt_shadow_layered.glsl
// array/layered spot+point path). Single source of truth for the terminate-
// on-first-hit occlusion query so the two masks cannot silently diverge.
//
// Requires GL_EXT_ray_query. The caller passes the TLAS handle and the ray
// flags explicitly (both shaders currently trace with
// gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT — preserved verbatim
// at each call site; alpha/opaque handling is a separate later item).
//
// Returns 1.0 when the ray reaches the light unobstructed (fully lit) and 0.0
// when a committed triangle intersection is found (occluded).

float traceRTShadowRay(accelerationStructureEXT tlas, uint rayFlags,
                       vec3 origin, float tMin, vec3 rayDir, float tMax) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, tlas,
        rayFlags,
        0xFF,
        origin,
        tMin,
        rayDir,
        tMax);

    while (rayQueryProceedEXT(rq)) {}

    float shadow = 1.0; // fully lit
    if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        shadow = 0.0; // occluded
    }
    return shadow;
}

#endif // RT_SHADOW_TRACE_COMMON_GLSL
