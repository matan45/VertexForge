#ifndef IBL_FUNCTIONS_GLSL
#define IBL_FUNCTIONS_GLSL

// IBL helper functions shared across all shaders that use split-sum approximation.
// Requires fresnelSchlickRoughness to be defined before including this file.

// Multi-scattering energy compensation (Fdez-Aguera 2019)
void multiScatterCompensation(vec3 F0, vec2 brdfLookup, float metallic,
                              out vec3 specularScale, out vec3 kD) {
    vec3 FssEss = F0 * brdfLookup.x + brdfLookup.y;
    float Ess = brdfLookup.x + brdfLookup.y;
    float Ems = 1.0 - Ess;
    vec3 Favg = F0 + (1.0 - F0) / 21.0;
    vec3 FmsEms = Ems * FssEss * Favg / max(1.0 - Favg * Ems, 1e-6);
    specularScale = FssEss + FmsEms;
    kD = (1.0 - FssEss - FmsEms) * (1.0 - metallic);
}

// Specular occlusion from AO (Lagarde/de Rousiers, Frostbite 2014)
float specularOcclusion(float NdotV, float ao, float roughness) {
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

#endif
