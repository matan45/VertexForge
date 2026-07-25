#ifndef HEX_TILING_GLSL
#define HEX_TILING_GLSL

// VK-1604: hex tile-and-blend anti-tiling for the periodic FFT displacement/normal maps.
// Ubisoft La Forge, "Making Waves in Ocean Surface Rendering using Tiling and Blending";
// lattice per Mikkelsen, "Practical Real-Time Hex-Tiling", JCGT 11(3).
//
// CPU twin: VFEngine/utilities/water/HexTiling.hpp — keep both in sync. CPU buoyancy applies the
// same blend so the physics surface matches the rendered one on whichever bands are tiled.
//
// The hash is INTEGER, not fract(sin(...)): sin() is not IEEE-exact on the GPU and is not
// consistent across vendors, and fract(sin(x)) degenerates for large x — ocean UVs are
// world-metres/patchSize and grow without bound as the camera travels.

uint hexHashUint(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

vec2 hexCellOffset(int cellX, int cellY)
{
    uint hx = uint(cellX) * 73856093u;
    uint hy = uint(cellY) * 19349663u;
    uint h = hexHashUint(hx ^ hy);
    return vec2(float(h & 0xFFFFu) / 65535.0, float((h >> 16) & 0xFFFFu) / 65535.0);
}

struct HexBlend {
    vec2 uv0;
    vec2 uv1;
    vec2 uv2;
    vec3 w;              // contrast-sharpened, sum == 1
    float varianceScale; // 1 / sqrt(dot(w, w)) — see below
};

// uv is in patch space (worldPos.xz / patchSize). cellScale = hex cells per patch.
HexBlend hexComputeBlend(vec2 uv, float cellScale, float contrast)
{
    vec2 scaledUV = uv * cellScale;

    // Skew into a triangular lattice.
    vec2 skewed = vec2(scaledUV.x, -0.57735027 * scaledUV.x + 1.15470054 * scaledUV.y);

    vec2 baseCell = floor(skewed);
    int baseX = int(baseCell.x);
    int baseY = int(baseCell.y);

    float fx = skewed.x - baseCell.x;
    float fy = skewed.y - baseCell.y;
    float fz = 1.0 - fx - fy;

    // Which of the two triangles in this rhombus contains the point.
    float s = step(0.0, -fz);
    float s2 = 2.0 * s - 1.0;

    vec3 w = vec3(-fz * s2, s - fy * s2, s - fx * s2);

    int si = int(s);
    ivec2 c0 = ivec2(baseX + si, baseY + si);
    ivec2 c1 = ivec2(baseX + si, baseY + 1 - si);
    ivec2 c2 = ivec2(baseX + 1 - si, baseY + si);

    // Sharpen then renormalize. max(w, 0) guards pow() against tiny negative fp noise.
    w = pow(max(w, vec3(0.0)), vec3(contrast));
    w /= max(w.x + w.y + w.z, 1e-8);

    HexBlend hb;
    hb.uv0 = uv + hexCellOffset(c0.x, c0.y);
    hb.uv1 = uv + hexCellOffset(c1.x, c1.y);
    hb.uv2 = uv + hexCellOffset(c2.x, c2.y);
    hb.w = w;
    // A plain weighted mean of three independent samples shrinks variance (down to 1/sqrt(3) at
    // a triangle centre), which visibly flattens the waves. Restoring it keeps the displacement
    // statistically identical to the untiled exemplar — and since FFT displacement is zero-mean
    // (the spectrum has no DC term), this variance-preserving blend IS the histogram-preserving
    // blend for it. Do not "upgrade" it to a Heitz-Neyret inverse-CDF LUT; that buys nothing here.
    hb.varianceScale = inversesqrt(max(dot(w, w), 1e-8));
    return hb;
}

// --- Sampling helpers -------------------------------------------------------------------
// Vertex stage: no derivatives exist, so use an explicit LOD. The FFT images are single-mip,
// so textureLod(..., 0.0) is exactly equivalent to the untiled texture() call it replaces.
//
// Fragment stage: MUST use textureGrad with derivatives of the CONTINUOUS (un-offset) UV. The
// per-cell offsets are discontinuous across cell edges, so implicit derivatives spike there and
// produce a blurred grid of seams plus shimmer under motion — the same hazard documented for
// triplanar sampling in material/terrain_material_generated.glsl (VK-1209).

// Displacement map: xyz = displacement (zero-mean -> variance-preserving),
//                   w   = foam (non-negative -> mean-preserving; a variance-preserving blend
//                         around a non-zero mean can go NEGATIVE and punch holes in the foam).
vec4 hexSampleDisplacementLod(sampler2D tex, HexBlend hb)
{
    vec4 s0 = textureLod(tex, hb.uv0, 0.0);
    vec4 s1 = textureLod(tex, hb.uv1, 0.0);
    vec4 s2 = textureLod(tex, hb.uv2, 0.0);

    vec3 disp = (hb.w.x * s0.xyz + hb.w.y * s1.xyz + hb.w.z * s2.xyz) * hb.varianceScale;
    float foam = hb.w.x * s0.w + hb.w.y * s1.w + hb.w.z * s2.w;
    return vec4(disp, foam);
}

// Normal map: directional, blend as vectors then let the caller normalize.
vec3 hexSampleNormalLod(sampler2D tex, HexBlend hb)
{
    return hb.w.x * textureLod(tex, hb.uv0, 0.0).xyz
         + hb.w.y * textureLod(tex, hb.uv1, 0.0).xyz
         + hb.w.z * textureLod(tex, hb.uv2, 0.0).xyz;
}

// Fragment-stage foam (the .w channel of the displacement map).
float hexSampleFoamGrad(sampler2D tex, HexBlend hb, vec2 uvDx, vec2 uvDy)
{
    return hb.w.x * textureGrad(tex, hb.uv0, uvDx, uvDy).w
         + hb.w.y * textureGrad(tex, hb.uv1, uvDx, uvDy).w
         + hb.w.z * textureGrad(tex, hb.uv2, uvDx, uvDy).w;
}

bool hexBandEnabled(uint bandMask, uint band)
{
    return (bandMask & (1u << band)) != 0u;
}

#endif // HEX_TILING_GLSL
