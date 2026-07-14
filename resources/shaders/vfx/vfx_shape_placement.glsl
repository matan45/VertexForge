// VK-1525 — shared spawn-shape placement math.
//
// SOURCE OF TRUTH for the ordered / path-driven placement. Included by
// vfx_particle_sim.glsl (GPU sim). Keep VFEngine/utilities/vfx/VFXShapePlacementMath.hpp
// (the doctest spec + CPU-preview implementation) in LOCKSTEP with this file.
//
// Also owns the SHAPE_* flag constants (packed into GPUEmitterConfig::shapeFlags),
// so the sim shader includes this instead of redefining them.
//
// Dependency-free (primitives only) so it can be included right after vfx_gpu_types.glsl.

const uint SHAPE_SPHERE = 256u;             // 1 << 8
const uint SHAPE_CONE = 512u;               // 1 << 9
const uint SHAPE_BOX = 1024u;               // 1 << 10
const uint SHAPE_TORUS = 2048u;             // 1 << 11 (VK-1525: a real 3-D torus; was a flat circle)
const uint SHAPE_EMIT_FROM_SURFACE = 4096u; // 1 << 12
const uint SHAPE_RANDOM_DIRECTION = 8192u;  // 1 << 13
const uint SHAPE_RING = 16384u;             // 1 << 14 (VK-1525: flat ring / arc / annulus)
const uint SHAPE_ORDERED = 32768u;          // 1 << 15 (VK-1525: draw the shape out in spawn order)
const uint SHAPE_LINE = 65536u;             // 1 << 16 (line segment along dims.xyz, centered)

const float VFXSP_TWO_PI = 6.28318530718;

// Spiral density for the shapes that sweep an angle as they draw out (keep in sync
// with VFXShapePlacementMath.hpp).
const float VFXSP_CONE_TURNS = 2.0;
const float VFXSP_SPHERE_TURNS = 4.0;
const float VFXSP_TORUS_COILS = 6.0;

// --- RNG primitives (bit-exact mirror of pcg_hash/randomFloat in the sim) --------

uint vfxspHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float vfxspRandomFloat(inout uint seed)
{
    seed = vfxspHash(seed);
    return float(seed) / float(0xFFFFFFFFu);
}

// Deterministic per-particle jitter seed for ordered placement (mirror of
// VFXShapePlacementMath.hpp::vfxspOrderedJitterSeed): emitter seed + quantized sweep
// progress + the particle's spawn slot (index within the frame's spawn batch).
// Frame-independent so seek/prewarm replay reproduce the same scatter.
uint vfxspOrderedJitterSeed(uint emitterSeed, float progress, uint spawnSlot)
{
    return vfxspHash(emitterSeed ^ vfxspHash(uint(progress * 65535.0) ^ spawnSlot));
}

// --- Ordered placement -----------------------------------------------------------

// On-curve point for `progress` in [0,1], per shape (dims packing matches ShapeConfig;
// Ring: x=radius, y=thickness, z=arcSpan, w=startAngle).
vec3 vfxspOrderedCurve(uint shapeFlags, vec4 dims, float progress)
{
    if ((shapeFlags & SHAPE_RING) != 0u)
    {
        float theta = dims.w + progress * dims.z;
        float radius = dims.x;
        return vec3(radius * cos(theta), 0.0, radius * sin(theta));
    }
    else if ((shapeFlags & SHAPE_TORUS) != 0u)
    {
        float theta = progress * VFXSP_TWO_PI;
        float minorAngle = progress * VFXSP_TWO_PI * VFXSP_TORUS_COILS;
        float ringDist = dims.x + dims.y * cos(minorAngle);
        return vec3(ringDist * cos(theta), dims.y * sin(minorAngle), ringDist * sin(theta));
    }
    else if ((shapeFlags & SHAPE_CONE) != 0u)
    {
        float y = progress * dims.y;
        float r = progress * dims.x * tan(dims.z);
        float theta = progress * VFXSP_TWO_PI * VFXSP_CONE_TURNS;
        return vec3(r * cos(theta), y, r * sin(theta));
    }
    else if ((shapeFlags & SHAPE_SPHERE) != 0u)
    {
        float radius = dims.x;
        float y = radius * (2.0 * progress - 1.0);
        float ringR = sqrt(max(0.0, radius * radius - y * y));
        float theta = progress * VFXSP_TWO_PI * VFXSP_SPHERE_TURNS;
        return vec3(ringR * cos(theta), y, ringR * sin(theta));
    }
    else if ((shapeFlags & SHAPE_BOX) != 0u)
    {
        vec3 half3 = dims.xyz;
        return mix(-half3, half3, progress);
    }
    else if ((shapeFlags & SHAPE_LINE) != 0u)
    {
        vec3 half3 = dims.xyz;
        return mix(-half3, half3, progress);
    }
    return vec3(0.0);
}

// Ordered placement = on-curve point + optional deterministic per-particle jitter.
vec3 vfxspOrderedPosition(uint shapeFlags, vec4 dims, float progress, float jitterAmt, uint jitterSeed)
{
    vec3 pos = vfxspOrderedCurve(shapeFlags, dims, progress);
    if (jitterAmt > 0.0)
    {
        uint s = jitterSeed;
        float jx = vfxspRandomFloat(s) * 2.0 - 1.0;
        float jy = vfxspRandomFloat(s) * 2.0 - 1.0;
        float jz = vfxspRandomFloat(s) * 2.0 - 1.0;
        pos += jitterAmt * vec3(jx, jy, jz);
    }
    return pos;
}
