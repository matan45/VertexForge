#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace material
{
    // CPU-side toon/cel shading profile (UE 5.8-style Toon Profile parity).
    // Serialized to a reusable `.vfToonProfile` JSON asset and referenced by
    // materials whose shadingModel == Toon. Uploaded verbatim into the 128-entry
    // GPU table (ToonProfileGpuTable) — the field set maps 1:1 onto ToonProfileGPU
    // (96 B, six vec4). Keep this struct free of GPU/graphics dependencies (it lives
    // in the Utilities StaticLib and is embedded in MaterialData).
    struct ToonProfile
    {
        // Diffuse banding
        glm::vec3 shadeColor{0.12f, 0.12f, 0.18f};   // deepest-shadow band tint
        glm::vec3 midColor{0.5f, 0.5f, 0.55f};       // mid band tint
        float shadowThreshold = 0.35f;               // shade -> mid band edge (0..1)
        float midThreshold = 0.65f;                  // mid -> lit band edge (0..1)
        float bandSmoothness = 0.03f;                // smoothstep half-width at each edge
        float giScale = 1.0f;                        // flat-GI ambient multiplier

        // Toon specular blob
        glm::vec3 specColor{1.0f, 1.0f, 1.0f};
        float specThreshold = 0.5f;                  // hard-blob cut on pow(NdotH, shininess)
        float specSmoothness = 0.02f;                // smoothstep half-width of the blob edge
        float specIntensity = 0.0f;                  // 0 = specular off by default
        float specShininess = 32.0f;                 // pow exponent (blob tightness)

        // Fresnel rim
        glm::vec3 rimColor{1.0f, 1.0f, 1.0f};
        float rimPower = 3.0f;                        // (1 - NdotV)^rimPower
        float rimIntensity = 0.0f;                    // 0 = rim off by default

        bool operator==(const ToonProfile& o) const
        {
            return shadeColor == o.shadeColor && midColor == o.midColor &&
                   shadowThreshold == o.shadowThreshold && midThreshold == o.midThreshold &&
                   bandSmoothness == o.bandSmoothness && giScale == o.giScale &&
                   specColor == o.specColor && specThreshold == o.specThreshold &&
                   specSmoothness == o.specSmoothness && specIntensity == o.specIntensity &&
                   specShininess == o.specShininess && rimColor == o.rimColor &&
                   rimPower == o.rimPower && rimIntensity == o.rimIntensity;
        }
        bool operator!=(const ToonProfile& o) const { return !(*this == o); }
    };
}
