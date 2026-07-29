#ifndef HEX_TILING_TERRAIN_GLSL
#define HEX_TILING_TERRAIN_GLSL

// VK-1612 — hex tile-and-blend stochastic sampling for terrain splat layers.
// Mikkelsen, "Practical Real-Time Hex-Tiling", JCGT 11(3) (reference implementation
// mmikk/hextile-demo); weight sharpening per Burley, "On Histogram-Preserving Blending for
// Randomized Texture Tiling", JCGT 8(4), Eq. 5.
//
// CPU twin: VFEngine/utilities/terrain/TerrainHexTiling.hpp — keep both in sync. The twin exists
// so the lattice, the weights and the blend's convexity are unit-testable without a GPU.
//
// DELIBERATELY NOT resources/shaders/water/hex_tiling.glsl, which VK-1604 wrote for the ocean:
//
//  1. That file's skew (hex_tiling.glsl:47) is the TRANSPOSE of Mikkelsen's, so its lattice is
//     not equilateral. Its basis inverts to (1, 0.5) and (0, 0.866), giving triangle sides
//     1.118 / 1.065 / 0.866 instead of 1 / 1 / 1 — roughly -15%..+10% anisotropy. Barycentric
//     weights are affine-invariant, so the ocean's own tests cannot see it, and on zero-mean FFT
//     displacement it is invisible. On albedo it is a directional bias, i.e. exactly the artifact
//     this story exists to remove. The correct skew is used below. (Fixing the water file is a
//     separate change: it would move the rendered ocean AND the CPU buoyancy surface, which
//     mirrors the same math for physics.)
//  2. Its helpers take `sampler2D` parameters; terrain samples bindlessTextures[nonuniformEXT(i)],
//     so these take a bindless index and do the indexing inside. This include must therefore
//     follow the bindlessTextures declaration.
//  3. Its HexBlend carries `varianceScale` = 1/sqrt(dot(w,w)), which is variance-preserving ABOUT
//     ZERO. Albedo is not zero-mean: at a triangle centre that multiplies colour by sqrt(3), and
//     the RVT albedo plane is R8G8B8A8_SRGB, so the overshoot would be clamped and permanently
//     baked into the page. Shipping a struct whose most tempting field is a trap is worse than
//     having two files.
//
// The hash is INTEGER (lowbias32, Chris Wellons), not fract(sin(...)): GPU sin() is neither
// IEEE-exact nor consistent across vendors and degenerates for large arguments, while terrain UVs
// grow without bound with world size. This is also why Mikkelsen's separate `hextiling_rws.h`
// (real-world-space) variant is not needed here — it exists to work around exactly that.

const float HEX_TERRAIN_FALLOFF_CONTRAST = 0.6; // Mikkelsen g_fallOffContrast

uint hexTerrainHashUint(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

struct HexTerrainBlend {
    vec2 uv[3];   // per-tap texture UV (offset, optionally rotated)
    vec2 dx[3];   // per-tap gradients, transformed by the SAME 2x2 as the UV
    vec2 dy[3];
    vec2 cs[3];   // per-tap (cos, sin); needed to bring a tangent-space normal back to the surface frame
    vec3 w;       // Burley-sharpened barycentric weights, sum == 1
};

// uv/dx/dy are the layer's texture UV and its screen-space gradients, which the includer computed
// in UNIFORM control flow (mesh_terrain.glsl / terrain_rvt_bake.glsl) — the composite's
// explicit-gradient contract. Every transform below is linear, so the transformed gradients stay
// exact and textureGrad's anisotropic footprint is preserved.
//   cellScale   hex cells per texture repeat.
//   gamma       Burley's exponent. 4 is his recommendation ("Ghosting is visible at gamma <= 2,
//               and tile structure is visible at gamma >= 8").
//   rotStrength 0 = translation only, which is what Mikkelsen's own demo ships by default.
HexTerrainBlend hexTerrainComputeBlend(vec2 uv, vec2 dx, vec2 dy,
                                       float cellScale, float gamma, float rotStrength)
{
    vec2 st = uv * cellScale;

    // Skew into an EQUILATERAL triangular lattice. Columns of the inverse are (1, 0) and
    // (0.5, 0.866025), whose three pairwise distances are all 1 — that is the property the water
    // twin loses. Mikkelsen's gridToSkewedGrid, in column-vector convention.
    vec2 skewed = vec2(st.x - 0.57735027 * st.y, 1.15470054 * st.y);

    vec2 baseCell = floor(skewed);
    float fx = skewed.x - baseCell.x;
    float fy = skewed.y - baseCell.y;
    float fz = 1.0 - fx - fy;

    // Which of the two triangles of this rhombus contains the point, branchlessly.
    float s = step(0.0, -fz);
    float s2 = 2.0 * s - 1.0;
    vec3 w = vec3(-fz * s2, s - fy * s2, s - fx * s2);

    int si = int(s);
    ivec2 baseId = ivec2(baseCell);
    ivec2 v[3];
    v[0] = baseId + ivec2(si,     si);
    v[1] = baseId + ivec2(si,     1 - si);
    v[2] = baseId + ivec2(1 - si, si);

    // Burley Eq. 5: w' = w^gamma / sum(w^gamma). This is what recovers the contrast a plain
    // weighted mean of three taps would lose — at barycentric (0.5, 0.25, 0.25) and gamma 4 the
    // weights become ~(0.94, 0.03, 0.03), i.e. very nearly a hard selection, and only a small
    // neighbourhood of the exact centroid blends appreciably. max(w, 0) guards pow() against tiny
    // negative floating-point noise, which would otherwise produce NaN.
    w = pow(max(w, vec3(0.0)), vec3(gamma));
    w /= max(w.x + w.y + w.z, 1e-8);

    HexTerrainBlend hb;
    hb.w = w;

    for (int i = 0; i < 3; ++i)
    {
        uint h = hexTerrainHashUint(uint(v[i].x) * 73856093u ^ uint(v[i].y) * 19349663u);

        // A translation by any amount is a valid re-tiling, because the exemplar repeats with
        // period 1 in UV.
        vec2 off = vec2(float(h & 0xFFFFu), float((h >> 16) & 0xFFFFu)) * (1.0 / 65535.0);

        // Rotation without transcendentals: hash a point in [-1,1]^2 and normalize it. The
        // angular distribution is very slightly corner-biased, which is irrelevant for
        // de-correlation, and it costs one rsqrt instead of a sin/cos pair per tap.
        //
        // Rotation matters more than it looks: Burley and Wronski both note that blending the SAME
        // periodic texture at different offsets can leave the taps ANTI-correlated, which "can
        // lead to the complete zeroing of the texture variation and detail". Rotating decorrelates
        // aligned features. It defaults to 0 anyway, matching Mikkelsen's own demo.
        uint hr = hexTerrainHashUint(h ^ 0x9E3779B9u);
        vec2 rnd = vec2(float(hr & 0xFFFFu), float((hr >> 16) & 0xFFFFu)) * (2.0 / 65535.0) - 1.0;
        vec2 cs = rnd * inversesqrt(max(dot(rnd, rnd), 1e-12));
        // Lerp toward identity, then renormalize so intermediate strengths stay a pure rotation
        // rather than a rotation-plus-shrink (which would degenerate at t = 0.5 for a 180-degree
        // pick). The epsilon turns that degenerate case into an arbitrary rotation — which is the
        // feature, not a failure. At rotStrength 0 this is identity to within inversesqrt's
        // permitted 2.5 ULP; unlike VK-1609's contrast, exactness is NOT a correctness invariant
        // here, because nothing downstream multiplies the result by zero.
        cs = mix(vec2(1.0, 0.0), cs, clamp(rotStrength, 0.0, 1.0));
        cs *= inversesqrt(max(dot(cs, cs), 1e-12));
        mat2 R = mat2(vec2(cs.x, cs.y), vec2(-cs.y, cs.x)); // columns: R = [[c,-s],[s,c]]

        // invSkew maps a lattice index back to a cell centre in st space: columns (1,0), (0.5, sqrt(3)/2).
        vec2 cen = vec2(float(v[i].x) + 0.5 * float(v[i].y), 0.86602540 * float(v[i].y));

        // Rotate about the cell centre in hex space, then return to UV space and translate.
        // cellScale cancels out of the gradients exactly: d/dx[(R*(st-cen)+cen)/cellScale]
        // = R * (dst/dx) / cellScale = R * dx, because dst/dx == dx * cellScale.
        hb.uv[i] = (R * (st - cen) + cen) / cellScale + off;
        hb.dx[i] = R * dx;
        hb.dy[i] = R * dy;
        hb.cs[i] = cs;
    }
    return hb;
}

// Albedo: Mikkelsen's hex2colTex. The luminance term breaks the tie at the triangle centroid,
// which is the one point where the weight ramp alone cannot help (all three weights are equal
// there, so pow() changes nothing) — and it costs ZERO extra fetches, since the three taps are
// already in registers.
//
// The result is a CONVEX combination: W >= 0 and sum(W) == 1, so it always lies inside the hull of
// the three taps. It can never clip or leave gamut. That is not a nicety given the sRGB8 RVT bake
// target — Heitz & Neyret say of the variance-preserving alternative that even with a correct mean
// it "overshoots (it creates too dark and too bright pixels)" and "does not prevent wrong colors
// or ghosting from appearing"; here that overshoot would be clamped and baked into the page.
vec3 hexTerrainSampleAlbedo(uint texIndex, HexTerrainBlend hb)
{
    vec3 c0 = textureGrad(bindlessTextures[nonuniformEXT(texIndex)], hb.uv[0], hb.dx[0], hb.dy[0]).rgb;
    vec3 c1 = textureGrad(bindlessTextures[nonuniformEXT(texIndex)], hb.uv[1], hb.dx[1], hb.dy[1]).rgb;
    vec3 c2 = textureGrad(bindlessTextures[nonuniformEXT(texIndex)], hb.uv[2], hb.dx[2], hb.dy[2]).rgb;

    const vec3 Lw = vec3(0.299, 0.587, 0.114); // ITU BT.601 luma, as in the reference implementation
    vec3 Dw = mix(vec3(1.0), vec3(dot(c0, Lw), dot(c1, Lw), dot(c2, Lw)), HEX_TERRAIN_FALLOFF_CONTRAST);

    vec3 W = Dw * hb.w;
    W /= max(W.x + W.y + W.z, 1e-8);
    return W.x * c0 + W.y * c1 + W.z * c2;
}

// Tangent-space normal. Each tap was fetched at a UV rotated by R_i, so its perturbation is
// expressed in a tangent basis rotated by R_i relative to the surface's. Undo that before summing.
//
// Derivation (column-vector convention): the tap samples a height field h at R*p + b, so the
// surface-space height is H(p) = h(R*p + b) and grad_p H = R^T * (grad h). A tangent-space normal
// is (-grad h, 1), hence the surface-frame normal is (R^T * n.xy, n.z) — rotation by -theta, i.e.
// the TRANSPOSE of the UV rotation, not the rotation itself. This is the same correction
// Mikkelsen applies to his surface-gradient representation; for a pure orthonormal rotation it
// needs none of that machinery, just four multiply-adds.
//
// Blended with the plain sharpened weights (no luminance term: it is meaningless for a direction),
// and left unnormalized — the composite already normalizes the accumulated normal.
vec3 hexTerrainSampleNormal(uint texIndex, HexTerrainBlend hb)
{
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 3; ++i)
    {
        vec3 n = textureGrad(bindlessTextures[nonuniformEXT(texIndex)], hb.uv[i], hb.dx[i], hb.dy[i]).xyz * 2.0 - 1.0;
        vec2 cs = hb.cs[i];
        // R^T * n.xy
        vec2 nxy = vec2(cs.x * n.x + cs.y * n.y, -cs.y * n.x + cs.x * n.y);
        acc += hb.w[i] * vec3(nxy, n.z);
    }
    return acc;
}

#endif // HEX_TILING_TERRAIN_GLSL
