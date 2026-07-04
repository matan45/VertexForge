#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp> // glm::simplex
#include <algorithm>         // std::clamp

// Divergence-free curl noise (Bridson 2007): F = curl(Psi), where the vector
// potential Psi = (psi1, psi2, psi3) is the existing scalar simplex sampled at
// three decorrelated offsets (reusing the Turbulence +0/+100/+200 convention),
// in Turbulence's scaled/scrolled sample space q = pos*frequency + time*scroll.
//
// The curl is taken with central finite differences (kCurlEpsilon). div(curl) is
// identically zero for any smooth field; discretely, the finite-difference shift
// operators commute exactly (IEEE addition is commutative), so the divergence of
// this field collapses to float rounding rather than truncation error.
//
// This is the single tested source of truth for the CPU force: VFXParticleSystem's
// applyForce(CurlNoiseForceConfig) and the divergence doctest both call it. The GLSL
// branch in vfx_particle_sim.glsl is a literal mirror of this stencil (with GPU's
// hand-ported simplexNoise3D standing in for glm::simplex, exactly as Turbulence does).
namespace vfx
{
    // Finite-difference step, shared verbatim with the GLSL mirror and the divergence test.
    inline constexpr float kCurlEpsilon = 0.01f;

    // fBM scalar potential at a decorrelation offset. Mirrors the Turbulence octave
    // loop (lacunarity 2, gain 0.5, capped at 4 octaves).
    inline float curlPotential(const glm::vec3& q, const glm::vec3& offset, int octaves)
    {
        if (octaves <= 1)
            return glm::simplex(q + offset);

        float amplitude = 1.0f;
        float frequency = 1.0f;
        float sum = 0.0f;
        for (int i = 0; i < octaves && i < 4; ++i)
        {
            sum += glm::simplex(q * frequency + offset) * amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        return sum;
    }

    // Pure curl-noise force at a world position and time. 12 simplex calls per octave
    // (6 partials x 2 central samples); result is scaled by strength.
    inline glm::vec3 evalCurlNoise(float strength, float frequency, float scrollSpeed,
                                   int octaves, const glm::vec3& pos, float time)
    {
        const int oct = std::clamp(octaves, 1, 4);
        const glm::vec3 q = pos * frequency + glm::vec3(time * scrollSpeed);
        const float inv2h = 1.0f / (2.0f * kCurlEpsilon);
        const glm::vec3 dx(kCurlEpsilon, 0.0f, 0.0f);
        const glm::vec3 dy(0.0f, kCurlEpsilon, 0.0f);
        const glm::vec3 dz(0.0f, 0.0f, kCurlEpsilon);
        const glm::vec3 o1(0.0f);
        const glm::vec3 o2(100.0f);
        const glm::vec3 o3(200.0f);

        const float dPsi3_dy = (curlPotential(q + dy, o3, oct) - curlPotential(q - dy, o3, oct)) * inv2h;
        const float dPsi2_dz = (curlPotential(q + dz, o2, oct) - curlPotential(q - dz, o2, oct)) * inv2h;
        const float dPsi1_dz = (curlPotential(q + dz, o1, oct) - curlPotential(q - dz, o1, oct)) * inv2h;
        const float dPsi3_dx = (curlPotential(q + dx, o3, oct) - curlPotential(q - dx, o3, oct)) * inv2h;
        const float dPsi2_dx = (curlPotential(q + dx, o2, oct) - curlPotential(q - dx, o2, oct)) * inv2h;
        const float dPsi1_dy = (curlPotential(q + dy, o1, oct) - curlPotential(q - dy, o1, oct)) * inv2h;

        return glm::vec3(dPsi3_dy - dPsi2_dz,  // curl.x = d(psi3)/dy - d(psi2)/dz
                         dPsi1_dz - dPsi3_dx,  // curl.y = d(psi1)/dz - d(psi3)/dx
                         dPsi2_dx - dPsi1_dy)  // curl.z = d(psi2)/dx - d(psi1)/dy
               * strength;
    }
}
