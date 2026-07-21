#ifndef REFLECTION_PROBES_GLSL
#define REFLECTION_PROBES_GLSL

// VK-1577 — local reflection probes: parallax-corrected specular that overrides the global IBL
// inside a probe's bounds.
//
// EVERY function below is a line-for-line transliteration of
// VFEngine/utilities/probe/ReflectionProbeMath.hpp, which is covered by
// VFEngine/tests/test_reflection_probe_math.cpp. When one changes the other MUST change with it —
// the doctests are the only executable check on this math, since the GPU path cannot be unit tested.

#include "reflection_probe_types.glsl"

// Component-wise reciprocal that never divides by zero, preserving sign so the slab test still
// picks the correct side for an axis-parallel ray (the resulting huge t is discarded by the min).
vec3 probeSafeInverse(vec3 v)
{
    const float eps = 1e-6;
    vec3 g;
    g.x = (abs(v.x) < eps) ? (v.x < 0.0 ? -eps : eps) : v.x;
    g.y = (abs(v.y) < eps) ? (v.y < 0.0 ? -eps : eps) : v.y;
    g.z = (abs(v.z) < eps) ? (v.z < 0.0 ? -eps : eps) : v.z;
    return 1.0 / g;
}

// Hermite falloff. Deliberately not linear: a linear ramp is only C0 and its derivative jump at the
// band edges reads as a crease where a large surface crosses a probe boundary.
float probeSmoothFalloff(float t)
{
    return t * t * (3.0 - 2.0 * t);
}

// Box influence weight, per axis against that axis' own normalized band, then min()'d — so the
// blend band stays a constant number of WORLD units on every axis even for a very non-cubic probe,
// and corners fade on whichever face is nearer instead of popping.
float probeWeightBox(vec3 pLocal, vec3 blendNorm)
{
    vec3 d = vec3(1.0) - abs(pLocal);
    vec3 w = d / max(blendNorm, vec3(1e-4));
    float t = clamp(min(w.x, min(w.y, w.z)), 0.0, 1.0);
    return probeSmoothFalloff(t);
}

float probeWeightSphere(vec3 fragWorld, vec3 center, float radius, float blendDistance)
{
    float dist = length(fragWorld - center);
    float t = clamp((radius - dist) / max(blendDistance, 1e-4), 0.0, 1.0);
    return probeSmoothFalloff(t);
}

// Lagarde box projection. A plain cubemap lookup assumes an infinitely distant environment, so a
// reflection depends only on direction — walk a mirror toward a wall and nothing changes. This
// intersects the reflection ray with the probe bounds and looks up the direction from the probe's
// CAPTURE POINT to that hit, which is where the captured texel actually lives.
//
// worldToLocal folds 1/halfExtents in, so mat3() of it is NOT orthonormal for a non-cubic probe and
// rLocal comes out sheared and non-unit. That is safe because ray/box intersection is affine
// invariant — the ray parameter survives the map. Two rules keep it true, and both are load-bearing:
// never normalize rLocal, and reconstruct the hit in LOCAL space before mapping back to world.
vec3 probeParallaxBox(vec3 R, vec3 fragWorld, GPUReflectionProbe pr)
{
    vec3 pLocal = (pr.worldToLocal * vec4(fragWorld, 1.0)).xyz;
    vec3 rLocal = mat3(pr.worldToLocal) * R;

    vec3 invR = probeSafeInverse(rLocal);
    vec3 tMax = (vec3(1.0) - pLocal) * invR;
    vec3 tMin = (vec3(-1.0) - pLocal) * invR;
    vec3 t = max(tMax, tMin);
    float dist = min(t.x, min(t.y, t.z));

    vec3 hitLocal = pLocal + rLocal * dist;
    vec3 hitWorld = (pr.localToWorld * vec4(hitLocal, 1.0)).xyz;

    vec3 dir = hitWorld - pr.positionRadius.xyz;
    float len = length(dir);
    return (len > 1e-6) ? (dir / len) : R;
}

// Sphere projection: the FAR intersection (where the ray exits), matching the box case.
vec3 probeParallaxSphere(vec3 R, vec3 fragWorld, GPUReflectionProbe pr)
{
    vec3 center = pr.positionRadius.xyz;
    float radius = pr.positionRadius.w;

    vec3 oc = fragWorld - center;
    float b = dot(oc, R);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc <= 0.0)
        return R;

    float t = -b + sqrt(disc);
    vec3 hitWorld = fragWorld + R * t;

    vec3 dir = hitWorld - center;
    float len = length(dir);
    return (len > 1e-6) ? (dir / len) : R;
}

// Front-to-back weight budget (the UE/HDRP model), NOT normalize-to-sum-1.
//
// This distinction decides whether the acceptance criterion is met. Normalizing weights to sum to 1
// would promote a LONE probe at influence 0.1 to full strength, producing a hard edge exactly at
// its boundary. Clamping each weight to the remaining budget instead gives all three behaviours at
// once: equal-priority overlaps cross-fade, a high-priority interior probe at full influence
// consumes the whole budget and correctly overrides the exterior probe it sits inside, and a lone
// probe near its edge leaves the remainder to the global environment.
//
// Because each weight is clamped against what is left, the RESULT DEPENDS ON ITERATION ORDER — the
// probes arrive pre-sorted (priority desc, volume asc, UUID asc) by
// ReflectionProbeBufferManager so the order is view-independent and stable frame to frame.
//
// `outRemaining` is how much of the reflection the caller should still take from the global IBL.
vec3 sampleReflectionProbes(vec3 R, vec3 fragWorldPos, float roughness, out float outRemaining)
{
    vec3 probeSpec = vec3(0.0);
    float remaining = 1.0;

    // Fixed trip count on a uniform value: no early `break`, so the loop counter stays dynamically
    // uniform and the sample index with it. Also textureLod only — the loop is divergent, so
    // implicit derivatives would be undefined here.
    for (uint i = 0u; i < probeData.probeCount; ++i)
    {
        GPUReflectionProbe pr = probeData.probes[i];

        // Cheap world-AABB reject before the matrix work.
        if (any(lessThan(fragWorldPos, pr.boundsMin.xyz)) ||
            any(greaterThan(fragWorldPos, pr.boundsMax.xyz)))
            continue;

        float weight;
        vec3 dir;
        if (pr.params.y == PROBE_SHAPE_SPHERE)
        {
            float radius = pr.positionRadius.w;
            // blendNorm.x == blendDistance / radius, so recover the world-space band.
            float blendDistance = pr.blendNormIntensity.x * radius;
            weight = probeWeightSphere(fragWorldPos, pr.positionRadius.xyz, radius, blendDistance);
            dir = probeParallaxSphere(R, fragWorldPos, pr);
        }
        else
        {
            vec3 pLocal = (pr.worldToLocal * vec4(fragWorldPos, 1.0)).xyz;
            weight = probeWeightBox(pLocal, pr.blendNormIntensity.xyz);
            dir = probeParallaxBox(R, fragWorldPos, pr);
        }

        float w = min(weight, remaining);
        if (w > 0.0)
        {
            // Intensity scales the SAMPLE, never the weight — scaling the weight would let a probe
            // with intensity 2 eat twice its share of the budget and starve the ones behind it.
            probeSpec += PROBE_SAMPLE(pr.params.x, dir, roughness * MAX_REFLECTION_LOD).rgb
                       * pr.blendNormIntensity.w * w;
            remaining -= w;
        }
    }

    outRemaining = remaining;
    return probeSpec;
}

#endif // REFLECTION_PROBES_GLSL
