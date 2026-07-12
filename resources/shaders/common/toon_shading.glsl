#ifndef TOON_SHADING_GLSL
#define TOON_SHADING_GLSL

// VK-1493 — pure UE 5.8-style toon/cel shading primitives. No buffer or light-struct
// dependencies, so this include is shared by BOTH the GPU-driven ubershader (SSBO-fed,
// via toon_lighting.glsl) and the material preview footer (UBO-fed) — guaranteeing the
// viewport and preview compute identical bands. Callers supply a ToonProfileGPU (mirror
// of render::gpudriven::ToonProfileGPU, 96 B / six vec4 — keep the layout in lockstep).

struct ToonProfileGPU {
    vec4 shadeColor;       // rgb = deepest-shadow tint            (a unused)
    vec4 midColor;         // rgb = mid-band tint                  (a unused)
    vec4 diffParams;       // x=shadowThreshold y=midThreshold z=bandSmoothness w=giScale
    vec4 specParams;       // x=specThreshold  y=specSmoothness   z=specIntensity w=specShininess
    vec4 specColorRimPow;  // xyz=specColor                        w=rimPower
    vec4 rimParams;        // xyz=rimColor                         w=rimIntensity
};

// 3-band diffuse. Shadow + normalized attenuation are folded into the band coordinate
// BEFORE the smoothstep, so a penumbra / distance falloff reads as one clean band edge
// (not a lit edge times a separate shadow edge = a double terminator). Half-Lambert wraps
// NdotL into [0,1]. litColor is the fully-lit base color (albedo * lightColor).
//
// shadeColor / midColor are TINT MULTIPLIERS (0..1), not flat replacement colours: the
// shade -> mid -> white ramp multiplies the lit base colour, so an albedo texture shows
// through in EVERY band (darkened/tinted toward the profile colours in shadow) instead of
// the shade/mid bands hiding it behind a flat colour.
vec3 toonApplyBands(ToonProfileGPU p, float NdotL, float atten01, float shadow, vec3 litColor) {
    float halfLambert = NdotL * 0.5 + 0.5;
    float t = clamp(halfLambert * shadow * atten01, 0.0, 1.0);

    float s = max(p.diffParams.z, 1e-4);                                  // bandSmoothness
    float midFactor = smoothstep(p.diffParams.x - s, p.diffParams.x + s, t); // shade -> mid
    float litFactor = smoothstep(p.diffParams.y - s, p.diffParams.y + s, t); // mid   -> lit

    vec3 tint = mix(p.shadeColor.rgb, p.midColor.rgb, midFactor);
    tint = mix(tint, vec3(1.0), litFactor);
    return litColor * tint;
}

// Thresholded pow(NdotH, shininess) hard blob -> specColor * intensity, gated by
// shadow * attenuation so the highlight vanishes in shadow / out of range.
vec3 toonSpecBlob(ToonProfileGPU p, vec3 N, vec3 V, vec3 L, float atten01, float shadow) {
    vec3 H = normalize(V + L);
    float ndh = clamp(dot(N, H), 0.0, 1.0);          // clamp >= 0: pow base must be non-negative
    float spec = pow(ndh, max(p.specParams.w, 1.0)); // specShininess
    float ss = max(p.specParams.y, 1e-4);            // specSmoothness
    float blob = smoothstep(p.specParams.x - ss, p.specParams.x + ss, spec);
    return blob * p.specParams.z * p.specColorRimPow.rgb * shadow * atten01;
}

// View-based Fresnel rim (light-independent; add once per fragment).
vec3 toonRim(ToonProfileGPU p, float NdotV) {
    float rim = pow(1.0 - clamp(NdotV, 0.0, 1.0), max(p.specColorRimPow.w, 1e-3)); // rimPower
    return rim * p.rimParams.w * p.rimParams.rgb;                                  // rimIntensity * rimColor
}

// Multi-light combine: keep the highest-luminance banded contribution so the dominant
// light defines the bands (crisp, count-independent — additive would wash the bands out).
float toonLuminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 toonCombineDiffuse(vec3 accum, vec3 contrib) {
    return (toonLuminance(contrib) > toonLuminance(accum)) ? contrib : accum;
}

#endif // TOON_SHADING_GLSL
