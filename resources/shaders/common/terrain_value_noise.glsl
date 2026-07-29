#ifndef TERRAIN_VALUE_NOISE_GLSL
#define TERRAIN_VALUE_NOISE_GLSL

// VK-1611 — GLSL twin of terrain::valueNoise2D in VFEngine/utilities/terrain/ValueNoise.hpp,
// the hash-based 2D value noise already shared by the vegetation and mesh brushes (VK-1578).
// Keep both files in sync.
//
// This is a bit-for-bit port, not a lookalike, and that is the reason to use procedural noise
// here instead of a macro TEXTURE (which is what UE's community macro-variation setup samples):
//   * every operation is 32-bit unsigned integer arithmetic, which wraps identically in GLSL and
//     C++, so the same world position hashes to the same cell value on both sides;
//   * the final `float(h & 0xFFFFFFu) / float(0xFFFFFF)` is exact in FP32 — 0xFFFFFF is 2^24-1,
//     the largest integer FP32 represents exactly — so there is no rounding to diverge on.
// That makes the macro-variation math CPU-unit-testable, which a texture fetch never could be.
// It also costs no bindless slot, no VRAM, and no extra fetch inside a composite that is already
// fetch-bound.
//
// Deliberately NOT fract(sin(...)): GPU sin() is neither IEEE-exact nor consistent across
// vendors, and it degenerates for large arguments — and terrain world coordinates grow without
// bound. The same reasoning is spelled out in water/hex_tiling.glsl.

uint terrainNoiseHashUint(int ix, int iz, uint seed)
{
    uint h = uint(ix) * 374761393u + uint(iz) * 668265263u + seed * 362437u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return h;
}

float terrainNoiseHash(int ix, int iz, uint seed)
{
    return float(terrainNoiseHashUint(ix, iz, seed) & 0xFFFFFFu) / float(0xFFFFFF);
}

// p is in NOISE CELLS (world metres * frequency). Returns [0, 1].
float terrainValueNoise2D(vec2 p, uint seed)
{
    int x0 = int(floor(p.x));
    int z0 = int(floor(p.y));

    float fx = p.x - float(x0);
    float fz = p.y - float(z0);

    // Smoothstep interpolant, matching the CPU mirror exactly.
    float ux = fx * fx * (3.0 - 2.0 * fx);
    float uz = fz * fz * (3.0 - 2.0 * fz);

    float n00 = terrainNoiseHash(x0,      z0,      seed);
    float n10 = terrainNoiseHash(x0 + 1,  z0,      seed);
    float n01 = terrainNoiseHash(x0,      z0 + 1,  seed);
    float n11 = terrainNoiseHash(x0 + 1,  z0 + 1,  seed);

    float nx0 = n00 + (n10 - n00) * ux;
    float nx1 = n01 + (n11 - n01) * ux;
    return nx0 + (nx1 - nx0) * uz;
}

#endif // TERRAIN_VALUE_NOISE_GLSL
