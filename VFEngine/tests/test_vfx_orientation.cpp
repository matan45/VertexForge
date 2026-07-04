#include <doctest.h>
#include <vfx/VFXOrientationMath.hpp>
#include <vfx/VFXOrientationMode.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

// ============================================================
// VK-1476: Mesh particle orientation modes.
// These pure helpers are the CPU spec for the GPU orientation
// math in resources/shaders/vfx/vfx_mesh_orientation.glsl —
// keep the two in lockstep. Every basis must be finite,
// orthonormal, and right-handed (det +1) so mesh winding holds.
// ============================================================

using vfx::VFXOrientationMode;
using vfx::orientationModeToGpuValue;
using vfx::orientationModeToString;
using vfx::stringToOrientationMode;
using vfx::vfxAxisLockBasis;
using vfx::vfxCameraFacingBasis;
using vfx::vfxComputeMeshOrientation;
using vfx::vfxoRandomUnitVector;
using vfx::vfxoRotateByQuat;
using vfx::vfxTumbleBasis;
using vfx::vfxVelocityForwardBasis;

namespace
{
    bool isFinite(const glm::mat3& m)
    {
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                if (!std::isfinite(m[c][r]))
                    return false;
        return true;
    }

    bool isOrthonormal(const glm::mat3& m)
    {
        for (int c = 0; c < 3; ++c)
            if (std::abs(glm::length(m[c]) - 1.0f) > 1e-4f)
                return false;
        if (std::abs(glm::dot(m[0], m[1])) > 1e-4f) return false;
        if (std::abs(glm::dot(m[0], m[2])) > 1e-4f) return false;
        if (std::abs(glm::dot(m[1], m[2])) > 1e-4f) return false;
        return true;
    }

    // Right-handed proper rotation: det ~ +1 (preserves back-face winding).
    bool isProperRotation(const glm::mat3& m)
    {
        return isOrthonormal(m) && std::abs(glm::determinant(m) - 1.0f) < 1e-4f;
    }

    void checkVecApprox(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
    {
        CHECK(a.x == doctest::Approx(b.x).epsilon(eps));
        CHECK(a.y == doctest::Approx(b.y).epsilon(eps));
        CHECK(a.z == doctest::Approx(b.z).epsilon(eps));
    }

    void checkMatApprox(const glm::mat3& a, const glm::mat3& b)
    {
        for (int c = 0; c < 3; ++c)
            checkVecApprox(a[c], b[c]);
    }
}

TEST_CASE("VFXOrientation: enum contract (gpu value + string roundtrip)")
{
    CHECK(orientationModeToGpuValue(VFXOrientationMode::VelocityForward) == 0u);
    CHECK(orientationModeToGpuValue(VFXOrientationMode::Tumble) == 1u);
    CHECK(orientationModeToGpuValue(VFXOrientationMode::AxisLock) == 2u);
    CHECK(orientationModeToGpuValue(VFXOrientationMode::CameraFacing) == 3u);

    for (auto m : {VFXOrientationMode::VelocityForward, VFXOrientationMode::Tumble,
                   VFXOrientationMode::AxisLock, VFXOrientationMode::CameraFacing})
    {
        CHECK(stringToOrientationMode(orientationModeToString(m)) == m);
    }

    CHECK(stringToOrientationMode("tumble") == VFXOrientationMode::Tumble);
    CHECK(stringToOrientationMode("axisLock") == VFXOrientationMode::AxisLock);
    // Missing / unknown keys fall back to the byte-identical default.
    CHECK(stringToOrientationMode("") == VFXOrientationMode::VelocityForward);
    CHECK(stringToOrientationMode("bogus") == VFXOrientationMode::VelocityForward);
}

TEST_CASE("VFXOrientation: VelocityForward no NaN at zero velocity")
{
    glm::mat3 b = vfxVelocityForwardBasis(glm::vec3(0.0f), 0.0f);
    CHECK(isFinite(b));
    CHECK(isProperRotation(b));
    // Zero velocity -> default nose (0,1,0) as column 2 (the forward axis).
    checkVecApprox(b[2], glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST_CASE("VFXOrientation: VelocityForward matches legacy formula")
{
    SUBCASE("velocity along +Z, no roll -> identity basis")
    {
        glm::mat3 b = vfxVelocityForwardBasis(glm::vec3(0.0f, 0.0f, 5.0f), 0.0f);
        CHECK(isProperRotation(b));
        checkVecApprox(b[2], glm::vec3(0.0f, 0.0f, 1.0f)); // forward = normalized velocity
        checkMatApprox(b, glm::mat3(1.0f));
    }

    SUBCASE("near-vertical velocity exercises the up-fallback branch")
    {
        glm::mat3 b = vfxVelocityForwardBasis(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
        CHECK(isFinite(b));
        CHECK(isProperRotation(b));
        checkVecApprox(b[2], glm::vec3(0.0f, 1.0f, 0.0f));
    }
}

TEST_CASE("VFXOrientation: VelocityForward roll rotates the right/up columns")
{
    // Forward +Z; roll +pi/2 about forward: right(+X)->+Y, up(+Y)->-X.
    glm::mat3 b = vfxVelocityForwardBasis(glm::vec3(0.0f, 0.0f, 1.0f), glm::half_pi<float>());
    CHECK(isProperRotation(b));
    checkVecApprox(b[0], glm::vec3(0.0f, 1.0f, 0.0f));
    checkVecApprox(b[1], glm::vec3(-1.0f, 0.0f, 0.0f));
    checkVecApprox(b[2], glm::vec3(0.0f, 0.0f, 1.0f));
}

TEST_CASE("VFXOrientation: random unit axis distribution + determinism")
{
    float minY = 1.0f, maxY = -1.0f;
    for (uint32_t seed = 1; seed <= 512; ++seed)
    {
        glm::vec3 a = vfxoRandomUnitVector(seed);
        CHECK(std::isfinite(a.x));
        CHECK(std::abs(glm::length(a) - 1.0f) < 1e-4f); // unit by construction
        CHECK(a.x >= -1.0001f);
        CHECK(a.x <= 1.0001f);
        minY = std::min(minY, a.y);
        maxY = std::max(maxY, a.y);
    }
    // Uniform-on-sphere -> the cos(phi) axis spans both hemispheres well.
    CHECK(minY < -0.5f);
    CHECK(maxY > 0.5f);

    // Deterministic per seed; different seeds differ.
    checkVecApprox(vfxoRandomUnitVector(1234u), vfxoRandomUnitVector(1234u));
    CHECK(glm::length(vfxoRandomUnitVector(1u) - vfxoRandomUnitVector(2u)) > 1e-3f);
}

TEST_CASE("VFXOrientation: AxisLock normalizes axis + guards zero axis")
{
    SUBCASE("axis magnitude does not matter (normalized)")
    {
        checkMatApprox(vfxAxisLockBasis(glm::vec3(0.0f, 5.0f, 0.0f), 1.1f),
                       vfxAxisLockBasis(glm::vec3(0.0f, 1.0f, 0.0f), 1.1f));
    }

    SUBCASE("zero axis falls back to (0,1,0) -> finite, no NaN")
    {
        glm::mat3 b = vfxAxisLockBasis(glm::vec3(0.0f), 0.7f);
        CHECK(isFinite(b));
        CHECK(isProperRotation(b));
    }

    SUBCASE("angle 0 -> identity")
    {
        checkMatApprox(vfxAxisLockBasis(glm::vec3(0.3f, 0.7f, -0.2f), 0.0f), glm::mat3(1.0f));
    }

    SUBCASE("+90 deg about +Y sends local +X to -Z")
    {
        glm::mat3 b = vfxAxisLockBasis(glm::vec3(0.0f, 1.0f, 0.0f), glm::half_pi<float>());
        CHECK(isProperRotation(b));
        checkVecApprox(b[0], glm::vec3(0.0f, 0.0f, -1.0f));
    }
}

TEST_CASE("VFXOrientation: Tumble is orthonormal, seed-unique, spins over angle")
{
    glm::mat3 a0 = vfxTumbleBasis(777u, 0.0f);
    CHECK(isProperRotation(a0));
    checkMatApprox(a0, glm::mat3(1.0f)); // angle 0 -> identity regardless of axis

    glm::mat3 aSpin = vfxTumbleBasis(777u, 1.3f);
    CHECK(isProperRotation(aSpin));
    CHECK(glm::length(aSpin[0] - a0[0]) > 1e-3f); // spin advances with angle

    // Deterministic; different seeds give different bases.
    checkMatApprox(vfxTumbleBasis(42u, 0.9f), vfxTumbleBasis(42u, 0.9f));
    CHECK(glm::length(vfxTumbleBasis(1u, 0.9f)[0] - vfxTumbleBasis(2u, 0.9f)[0]) > 1e-4f);
}

TEST_CASE("VFXOrientation: CameraFacing aligns to camera basis with roll")
{
    glm::mat3 cam(1.0f); // right=+X, up=+Y, toward-camera=+Z

    SUBCASE("no roll -> equals the camera basis")
    {
        checkMatApprox(vfxCameraFacingBasis(cam, 0.0f), cam);
    }

    SUBCASE("roll stays in the camera plane; forward axis is unchanged")
    {
        glm::mat3 b = vfxCameraFacingBasis(cam, glm::half_pi<float>());
        CHECK(isProperRotation(b));
        checkVecApprox(b[0], glm::vec3(0.0f, 1.0f, 0.0f));
        checkVecApprox(b[1], glm::vec3(-1.0f, 0.0f, 0.0f));
        checkVecApprox(b[2], cam[2]); // toward-camera axis preserved
    }
}

TEST_CASE("VFXOrientation: rotateByQuat parity with the sim shader")
{
    // Identity quat is a no-op.
    checkVecApprox(vfxoRotateByQuat(glm::vec3(3.0f, -2.0f, 1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
                   glm::vec3(3.0f, -2.0f, 1.0f));

    // 90 deg about +Y sends +X to -Z (matches vfx_particle_sim.glsl rotateByQuat).
    float h = glm::quarter_pi<float>();
    glm::vec4 q(0.0f, std::sin(h), 0.0f, std::cos(h));
    checkVecApprox(vfxoRotateByQuat(glm::vec3(1.0f, 0.0f, 0.0f), q), glm::vec3(0.0f, 0.0f, -1.0f));
}

TEST_CASE("VFXOrientation: dispatcher routes each mode to its basis")
{
    const glm::vec3 vel(1.0f, 2.0f, 3.0f);
    const float rotation = 0.6f;
    const uint32_t seed = 90210u;
    const float age = 1.5f;
    const glm::vec4 params(0.0f, 1.0f, 0.0f, 2.0f); // axis (0,1,0), spin rate 2 rad/s
    const glm::mat3 cam(1.0f);

    // VelocityForward is the byte-identical default regression guard.
    checkMatApprox(vfxComputeMeshOrientation(VFXOrientationMode::VelocityForward, vel, rotation, seed, age, params, cam),
                   vfxVelocityForwardBasis(vel, rotation));

    // AxisLock: angle = rate * age about params.xyz.
    checkMatApprox(vfxComputeMeshOrientation(VFXOrientationMode::AxisLock, vel, rotation, seed, age, params, cam),
                   vfxAxisLockBasis(glm::vec3(params), params.w * age));

    // CameraFacing: from the camera basis + roll; velocity/params ignored.
    checkMatApprox(vfxComputeMeshOrientation(VFXOrientationMode::CameraFacing, vel, rotation, seed, age, params, cam),
                   vfxCameraFacingBasis(cam, rotation));

    // Tumble: per-particle jittered rate; still orthonormal + deterministic.
    glm::mat3 t = vfxComputeMeshOrientation(VFXOrientationMode::Tumble, vel, rotation, seed, age, params, cam);
    CHECK(isProperRotation(t));
    checkMatApprox(t, vfxComputeMeshOrientation(VFXOrientationMode::Tumble, vel, rotation, seed, age, params, cam));

    // A non-camera mode ignores camBasis entirely.
    checkMatApprox(vfxComputeMeshOrientation(VFXOrientationMode::AxisLock, vel, rotation, seed, age, params, glm::mat3(0.0f)),
                   vfxComputeMeshOrientation(VFXOrientationMode::AxisLock, vel, rotation, seed, age, params, cam));
}
