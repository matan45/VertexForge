#pragma once

#include "VFXOrientationMode.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

// VK-1476 — CPU mirror of the mesh-particle orientation math.
//
// The GLSL file resources/shaders/vfx/vfx_mesh_orientation.glsl is the SOURCE OF
// TRUTH (it runs on the GPU for both runtime and the editor preview). This header
// is a faithful, doctest-verifiable mirror: keep the two in LOCKSTEP. Every
// function returns a glm::mat3 whose columns are the mesh-local X,Y,Z axes
// expressed in world space (a proper rotation, det +1, so back-face winding is
// preserved) — i.e. worldPos = particlePos + basis * (meshVertex * size).
//
// The RNG (vfxoHash/vfxoRandomFloat) and quaternion rotate mirror the primitives
// in resources/shaders/vfx/vfx_particle_sim.glsl (pcg_hash :151, randomFloat :158,
// generateSpherePosition direction :180, rotateByQuat :809). They are prefixed
// `vfxo` so a future shared-include never clashes with the sim's identifiers.
namespace vfx
{
    // --- RNG primitives (bit-exact mirror of vfx_particle_sim.glsl) ----------

    inline uint32_t vfxoHash(uint32_t v)
    {
        uint32_t state = v * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    // Advances `seed` and returns a float in [0,1]. Matches GLSL randomFloat.
    inline float vfxoRandomFloat(uint32_t& seed)
    {
        seed = vfxoHash(seed);
        return static_cast<float>(seed) / static_cast<float>(0xFFFFFFFFu);
    }

    // Uniformly-distributed unit vector on the sphere, deterministic from `seed`.
    // Mirrors generateSpherePosition's direction (theta/u/phi) with a salt so the
    // axis is decorrelated from vfxoRateJitter (which salts differently).
    inline glm::vec3 vfxoRandomUnitVector(uint32_t seed)
    {
        uint32_t s = seed ^ 0x9E3779B9u;
        float theta = vfxoRandomFloat(s) * 6.28318530718f;
        float u = vfxoRandomFloat(s) * 2.0f - 1.0f;
        u = std::clamp(u, -1.0f, 1.0f);
        float phi = std::acos(u);
        float sinPhi = std::sin(phi);
        return glm::vec3(sinPhi * std::cos(theta), std::cos(phi), sinPhi * std::sin(theta));
    }

    // Per-particle spin-rate multiplier in [0.5, 1.5], deterministic from `seed`.
    // Salted separately from the axis so rate and axis vary independently.
    inline float vfxoRateJitter(uint32_t seed)
    {
        uint32_t s = seed ^ 0x85EBCA6Bu;
        return 0.5f + vfxoRandomFloat(s);
    }

    // Rotate v by quaternion q = (xyz = axis*sin(a/2), w = cos(a/2)).
    // Mirrors vfx_particle_sim.glsl:809 rotateByQuat.
    inline glm::vec3 vfxoRotateByQuat(glm::vec3 v, glm::vec4 q)
    {
        glm::vec3 u(q.x, q.y, q.z);
        float s = q.w;
        return 2.0f * glm::dot(u, v) * u + (s * s - glm::dot(u, u)) * v + 2.0f * s * glm::cross(u, v);
    }

    // --- Per-mode bases -------------------------------------------------------

    // Rotation matrix that spins the identity basis by `angle` about a UNIT axis.
    inline glm::mat3 vfxoAxisAngleBasis(glm::vec3 unitAxis, float angle)
    {
        float halfAngle = angle * 0.5f;
        float sh = std::sin(halfAngle);
        glm::vec4 q(unitAxis.x * sh, unitAxis.y * sh, unitAxis.z * sh, std::cos(halfAngle));
        return glm::mat3(vfxoRotateByQuat(glm::vec3(1.0f, 0.0f, 0.0f), q),
                         vfxoRotateByQuat(glm::vec3(0.0f, 1.0f, 0.0f), q),
                         vfxoRotateByQuat(glm::vec3(0.0f, 0.0f, 1.0f), q));
    }

    // Mode 0. Verbatim port of vfx_mesh_particle.glsl:66-83 — mesh-local +Z follows
    // velocity, roll about it by `rotation`. Zero-velocity falls back to forward
    // (0,1,0); near-vertical velocity swaps the reference up. Byte-identical default.
    inline glm::mat3 vfxVelocityForwardBasis(glm::vec3 velocity, float rotation)
    {
        glm::vec3 forward(0.0f, 1.0f, 0.0f);
        float speed = glm::length(velocity);
        if (speed > 0.001f)
        {
            forward = velocity / speed;
        }

        glm::vec3 up = std::abs(forward.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(up, forward));
        up = glm::cross(forward, right);

        float cosR = std::cos(rotation);
        float sinR = std::sin(rotation);
        glm::vec3 rotRight = right * cosR + up * sinR;
        glm::vec3 rotUp = -right * sinR + up * cosR;
        return glm::mat3(rotRight, rotUp, forward);
    }

    // Mode 2. Spin about a fixed axis by `angle`. Guards a zero/degenerate axis by
    // falling back to (0,1,0) so normalize never produces NaN.
    inline glm::mat3 vfxAxisLockBasis(glm::vec3 axis, float angle)
    {
        float len = glm::length(axis);
        glm::vec3 unitAxis = (len < 1e-6f) ? glm::vec3(0.0f, 1.0f, 0.0f) : axis / len;
        return vfxoAxisAngleBasis(unitAxis, angle);
    }

    // Mode 1. Per-particle random axis (from spawnSeed) spun by `angle`.
    inline glm::mat3 vfxTumbleBasis(uint32_t spawnSeed, float angle)
    {
        return vfxoAxisAngleBasis(vfxoRandomUnitVector(spawnSeed), angle);
    }

    // Mode 3. Align to the camera basis (columns: right, up, toward-camera) and roll
    // about the toward-camera axis by `rotation`, exactly like the velocity-forward roll.
    inline glm::mat3 vfxCameraFacingBasis(const glm::mat3& camBasis, float rotation)
    {
        glm::vec3 camRight = camBasis[0];
        glm::vec3 camUp = camBasis[1];
        glm::vec3 camForward = camBasis[2];
        float cosR = std::cos(rotation);
        float sinR = std::sin(rotation);
        glm::vec3 rotRight = camRight * cosR + camUp * sinR;
        glm::vec3 rotUp = -camRight * sinR + camUp * cosR;
        return glm::mat3(rotRight, rotUp, camForward);
    }

    // Dispatcher — the single entry point mirrored by the GLSL function.
    //   params.xyz = axis-lock axis (world), params.w = spin rate (rad/s).
    //   age        = seconds since spawn (particle lifetime, counts up).
    // Tumble/AxisLock spin by rate*age (Tumble additionally jitters the rate per
    // particle); VelocityForward/CameraFacing roll by `rotation`.
    inline glm::mat3 vfxComputeMeshOrientation(VFXOrientationMode mode, glm::vec3 velocity,
                                               float rotation, uint32_t spawnSeed, float age,
                                               glm::vec4 params, const glm::mat3& camBasis)
    {
        switch (mode)
        {
        case VFXOrientationMode::Tumble:
        {
            float rate = params.w * vfxoRateJitter(spawnSeed);
            return vfxTumbleBasis(spawnSeed, rate * age);
        }
        case VFXOrientationMode::AxisLock:
        {
            float rate = params.w;
            return vfxAxisLockBasis(glm::vec3(params), rate * age);
        }
        case VFXOrientationMode::CameraFacing:
            return vfxCameraFacingBasis(camBasis, rotation);
        case VFXOrientationMode::VelocityForward:
        default:
            return vfxVelocityForwardBasis(velocity, rotation);
        }
    }
}
