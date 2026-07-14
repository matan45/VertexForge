#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <cmath>

namespace math
{
    // Shared NaN/Inf guards. Transform decomposition and gizmo math can produce
    // non-finite components on degenerate input; these are the single source of
    // truth used by the viewport gizmo, prefab rig, and transform service.
    inline bool isFinite(const glm::vec3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    inline bool isFinite(const glm::mat4& m)
    {
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                if (!std::isfinite(m[c][r])) return false;
            }
        }
        return true;
    }

    struct DecomposedTransform
    {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};  // Euler angles in degrees (XYZ order)
        glm::vec3 scale{1.0f};
    };

    // Decompose a transformation matrix into position, rotation (Euler degrees), and scale
    // Rotation uses XYZ order to match TransformComponent::getMatrix()
    inline DecomposedTransform decomposeMatrix(const glm::mat4& matrix)
    {
        DecomposedTransform result;
        glm::quat rotationQuat;
        glm::vec3 skew;
        glm::vec4 perspective;

        glm::decompose(matrix, result.scale, rotationQuat, result.position, skew, perspective);

        // Extract Euler angles in XYZ order (matching compose order)
        // Build rotation matrix from quaternion and extract XYZ angles
        glm::mat4 rotationMatrix = glm::mat4_cast(rotationQuat);
        float x, y, z;
        glm::extractEulerAngleXYZ(rotationMatrix, x, y, z);
        result.rotation = glm::degrees(glm::vec3(x, y, z));

        return result;
    }

    // Compose a transformation matrix from position, rotation (Euler degrees), and scale
    inline glm::mat4 composeMatrix(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale)
    {
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, position);
        transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        transform = glm::scale(transform, scale);
        return transform;
    }

    // Unit forward vector from an Euler rotation in DEGREES (x = pitch, y = yaw),
    // in the engine's right-handed, -Z-forward convention. Single source of truth
    // for AudioAPI (play-time source direction) and AudioSceneUpdater (per-frame
    // emitter follow + listener forward). Extracted verbatim from the previously
    // duplicated inline math so behaviour is unchanged.
    inline glm::vec3 forwardFromEulerDegrees(const glm::vec3& eulerDegrees)
    {
        const float yawRad = glm::radians(eulerDegrees.y);
        const float pitchRad = glm::radians(eulerDegrees.x);
        glm::vec3 forward;
        forward.x = -std::sin(yawRad) * std::cos(pitchRad);
        forward.y = std::sin(pitchRad);
        forward.z = -std::cos(yawRad) * std::cos(pitchRad);
        return glm::normalize(forward);
    }

    // Dirty-check predicate: true when b has moved from a by more than eps.
    // Squared-distance compare (no sqrt); reused for both position and direction deltas.
    inline bool positionMovedBeyond(const glm::vec3& a, const glm::vec3& b, float eps)
    {
        const glm::vec3 d = b - a;
        return glm::dot(d, d) > eps * eps;
    }

    // VK-1506 doppler: per-frame velocity from a finite position difference.
    // Returns zero for dt <= 0 (paused frame / first sample) and when the implied
    // speed exceeds maxSpeed (teleport guard — a warp must not chirp). Squared
    // compare avoids the sqrt. maxSpeed is expected to be > 0.
    inline glm::vec3 computeClampedVelocity(const glm::vec3& prev, const glm::vec3& curr,
                                            float dt, float maxSpeed)
    {
        if (dt <= 0.0f) return glm::vec3(0.0f);
        const glm::vec3 v = (curr - prev) / dt;
        if (glm::dot(v, v) > maxSpeed * maxSpeed) return glm::vec3(0.0f);
        return v;
    }

    // VK-1511: polar offset of an audition source relative to a listener, expressed
    // in the listener's own basis. Returns the WORLD vector to ADD to the listener
    // position. Engine convention (right-handed, -Z forward, +Y up):
    //   azimuth     0deg -> +forward  (in front)
    //   azimuth   +90deg -> +right    (right = normalize(cross(forward, up)))
    //   elevation +90deg -> +up
    // The passed-in up is re-orthogonalized (Gram-Schmidt), so a listener up that is
    // not exactly perpendicular to forward still yields an orthonormal basis. A
    // degenerate forward, or an up parallel to forward, falls back to a world
    // reference axis so the result is always finite (no NaN).
    inline glm::vec3 polarOffsetInBasis(const glm::vec3& forwardIn, const glm::vec3& upIn,
                                        float distance, float azimuthDeg, float elevationDeg)
    {
        // Normalize forward; fall back to engine -Z if degenerate.
        glm::vec3 forward = forwardIn;
        const float fLen = glm::length(forward);
        forward = (fLen > 1e-6f) ? forward / fLen : glm::vec3(0.0f, 0.0f, -1.0f);

        // right = forward x up. If up is parallel to forward the cross collapses;
        // pick a reference axis that isn't parallel to forward and rebuild.
        glm::vec3 right = glm::cross(forward, upIn);
        if (glm::dot(right, right) < 1e-12f)
        {
            const glm::vec3 ref = (std::abs(forward.y) < 0.99f)
                ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            right = glm::cross(forward, ref);
        }
        right = glm::normalize(right);

        const glm::vec3 up = glm::normalize(glm::cross(right, forward));  // re-orthogonalized

        const float az = glm::radians(azimuthDeg);
        const float el = glm::radians(elevationDeg);
        const glm::vec3 dir = std::cos(el) * (std::cos(az) * forward + std::sin(az) * right)
                            + std::sin(el) * up;
        return distance * dir;
    }
}
