#ifndef VFX_PBR_SHADING_GLSL
#define VFX_PBR_SHADING_GLSL

// VK-1526: shared PBR surface assembly for VFX mesh-render particles that reference a .vfMat/.vfMatInstance.
// Descriptor-independent (operates on already-sampled texels + scalars) so BOTH the runtime pipeline
// (vfx_mesh_particle.glsl, sampling the VK-1481 bindless table) and the preview pipeline
// (vfx_mesh_preview.glsl, sampling bound material samplers) include it and build an IDENTICAL surface. The
// light accumulation differs per pipeline (runtime = clustered scene lights, preview = one key directional),
// but the surface assembly + BRDF core are shared => "preview matches runtime" for the material response.
//
// VFX mesh-material flag bits (mirror of C++ render::vfx::MeshMaterialFlags). Shared by the runtime
// (vfx_mesh_particle.glsl) and preview (vfx_mesh_preview.glsl) mesh shaders.
const uint VFX_MAT_HAS_MATERIAL = 1u;
const uint VFX_MAT_USES_ORM     = 2u;
const uint VFX_MAT_HAS_NORMAL   = 4u;
const uint VFX_MAT_HAS_EMISSIVE = 8u;

// Perturb the geometric normal by a tangent-space normal map using a screen-space derivative cotangent
// frame — the VFX mesh vertex layout has position/normal/texcoord only, no tangent attribute. Falls back to
// the geometric normal when the derivative basis is degenerate (zero-area UV / flat derivatives).
vec3 vfxPerturbNormal(vec3 Ngeo, vec3 worldPos, vec2 uv, vec3 tangentNormal)
{
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 N = normalize(Ngeo);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float maxLen = max(dot(T, T), dot(B, B));
    if (maxLen <= 0.0) {
        return N; // degenerate basis -> keep the geometric normal
    }
    float invmax = inversesqrt(maxLen);
    mat3 TBN = mat3(T * invmax, B * invmax, N);
    vec3 perturbed = TBN * tangentNormal;
    if (dot(perturbed, perturbed) <= 0.0) {
        return N;
    }
    return normalize(perturbed);
}

struct VFXSurface {
    vec3 albedo;
    vec3 N;
    float metallic;
    float roughness;
    float ao;
    vec3 emissive;
    float alpha;
    vec3 F0;
};

// Assemble the PBR surface from already-sampled texels + material scalars + the particle color. `flags` are
// the VFX_MAT_* bits. baseColorTexel/emissiveTexel are sRGB-decoded by the sampler; normalTexel/ormTexel are
// linear. particleColor carries the VFX tint / ColorOverLifetime, kept on top of the material albedo tint.
VFXSurface vfxBuildMeshSurface(
    uint flags,
    vec4 baseColorTexel, vec3 normalTexel, vec3 ormTexel, vec3 emissiveTexel,
    float metallicScalar, float roughnessScalar, float aoScalar, float emissionStrength,
    vec4 albedoTint, vec4 particleColor,
    vec3 geoN, vec3 worldPos, vec2 uv)
{
    VFXSurface s;
    s.albedo = baseColorTexel.rgb * albedoTint.rgb * particleColor.rgb;
    s.alpha = baseColorTexel.a * albedoTint.a * particleColor.a;

    // Normal: perturb by the map only when present, else use the geometric normal.
    if ((flags & VFX_MAT_HAS_NORMAL) != 0u) {
        vec3 tn = normalize(normalTexel * 2.0 - 1.0);
        s.N = vfxPerturbNormal(geoN, worldPos, uv, tn);
    } else {
        s.N = normalize(geoN);
    }

    // Metallic / roughness / AO: from the packed ORM map (R=AO, G=Rough, B=Metal) or the scalar fallbacks.
    if ((flags & VFX_MAT_USES_ORM) != 0u) {
        s.ao        = ormTexel.r;
        s.roughness = ormTexel.g;
        s.metallic  = ormTexel.b;
    } else {
        s.ao        = aoScalar;
        s.roughness = roughnessScalar;
        s.metallic  = metallicScalar;
    }
    s.roughness = clamp(s.roughness, 0.04, 1.0);

    s.emissive = ((flags & VFX_MAT_HAS_EMISSIVE) != 0u ? emissiveTexel : vec3(0.0)) * emissionStrength;
    s.F0 = mix(vec3(0.04), s.albedo, s.metallic);
    return s;
}

#endif // VFX_PBR_SHADING_GLSL
