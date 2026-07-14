#include <doctest.h>

// VK-1502 -- CPU-only coverage for GPU VFX depth-buffer collision. The actual depth sampling / bounce
// runs in the sim compute shader (GPU-verified in-engine); these tests lock down the parts the shader
// and the C++ side must agree on: the 512-byte config ABI (the two new tail floats @504/508), the
// modifier flag bit, the camera struct layout, the CPU authoring defaults, and the analytic
// view-space reconstruction + collision-response math the shader implements.

#include "render/vfx/compute/GPUVFXTypes.hpp"
#include "render/vfx/billboard/VFXBillboardTypes.hpp"
#include <vfx/VFXTypes.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>

namespace
{
    // Mirror of reconstructViewPosDC() in vfx_particle_sim.glsl (standard perspective RH_ZO, depth [0,1]).
    glm::vec3 reconstructViewPos(const glm::mat4& proj, glm::vec2 uv, float ndcZ)
    {
        float viewZ = proj[3][2] / (-ndcZ - proj[2][2]);
        glm::vec2 ndcXY = uv * 2.0f - 1.0f;
        float viewX = ndcXY.x * (-viewZ) / proj[0][0];
        float viewY = ndcXY.y * (-viewZ) / proj[1][1];
        return glm::vec3(viewX, viewY, viewZ);
    }

    // Mirror of the shared collision-response kernel (analytic / terrain / depth all use this).
    glm::vec3 collisionResponse(glm::vec3 velocity, glm::vec3 n, float friction, float bounce)
    {
        float vn = glm::dot(velocity, n);
        if (vn < 0.0f)
        {
            glm::vec3 vNormal = n * vn;
            glm::vec3 vTangent = velocity - vNormal;
            return vTangent * (1.0f - friction) - vNormal * bounce;
        }
        return velocity;
    }
}

TEST_SUITE("VFXDepthCollision")
{
    TEST_CASE("GPUEmitterConfig tail ABI is preserved (512 bytes, new floats at 504/508)")
    {
        using render::vfx::GPUEmitterConfig;
        CHECK(sizeof(GPUEmitterConfig) == 512);
        CHECK(offsetof(GPUEmitterConfig, depthCollisionThickness) == 504);
        CHECK(offsetof(GPUEmitterConfig, depthCollisionNormalInfluence) == 508);

        // VK-1501's eventChildSlot still sits just before the new floats.
        CHECK(offsetof(GPUEmitterConfig, eventChildSlot) == 500);
    }

    TEST_CASE("DepthCollision modifier flag is bit 25 and disjoint from its neighbours")
    {
        using namespace render::vfx;
        CHECK(ModifierFlags::DepthCollision == (1u << 25));
        CHECK(ModifierFlags::DepthCollision == 0x02000000u);

        // Must not collide with any existing modifier/force/shape/flipbook bit.
        CHECK((ModifierFlags::DepthCollision & ModifierFlags::SizeBySpeed) == 0u);   // 1<<22
        CHECK((ModifierFlags::DepthCollision & ModifierFlags::ColorBySpeed) == 0u);  // 1<<23
        CHECK((ModifierFlags::DepthCollision & ModifierFlags::GlowOverLifetime) == 0u);
        CHECK((ModifierFlags::DepthCollision & ForceFlags::KillVolume) == 0u);       // 1<<20
        CHECK((ModifierFlags::DepthCollision & FlipbookFlags::FrameBlend) == 0u);    // 1<<21
    }

    TEST_CASE("GPUEmitterConfig defaults keep depth collision disabled/neutral")
    {
        render::vfx::GPUEmitterConfig cfg{};
        // Disabled: no DepthCollision bit set by default.
        CHECK((cfg.modifierFlags & render::vfx::ModifierFlags::DepthCollision) == 0u);
        CHECK(cfg.depthCollisionThickness == doctest::Approx(0.25f));
        CHECK(cfg.depthCollisionNormalInfluence == doctest::Approx(1.0f));
    }

    TEST_CASE("GPUVFXCameraUBO layout: depth-collision state repurposes the two former pad words")
    {
        using render::vfx::GPUVFXCameraUBO;
        CHECK(sizeof(GPUVFXCameraUBO) == 160);
        CHECK(offsetof(GPUVFXCameraUBO, depthCollisionActive) == 152);
        CHECK(offsetof(GPUVFXCameraUBO, prevDepthSlot) == 156);

        GPUVFXCameraUBO cam{};
        CHECK(cam.depthCollisionActive == 0u); // off by default -> sim never samples depth
        CHECK(cam.prevDepthSlot == 0u);
    }

    TEST_CASE("CPU emitter authoring defaults match EmitterDefaults")
    {
        render::vfx::VFXEmitterConfig cpu{};
        CHECK(cpu.depthCollisionEnabled == false);
        CHECK(cpu.depthCollisionThickness == doctest::Approx(vfx::EmitterDefaults::DEPTH_COLLISION_THICKNESS));
        CHECK(cpu.depthCollisionNormalInfluence == doctest::Approx(vfx::EmitterDefaults::DEPTH_COLLISION_NORMAL_INFLUENCE));

        CHECK(vfx::EmitterDefaults::DEPTH_COLLISION_ENABLED == false);
        CHECK(vfx::EmitterDefaults::DEPTH_COLLISION_THICKNESS == doctest::Approx(0.25f));
        CHECK(vfx::EmitterDefaults::DEPTH_COLLISION_NORMAL_INFLUENCE == doctest::Approx(1.0f));
    }

    TEST_CASE("Analytic view-space reconstruction round-trips through a perspective RH_ZO projection")
    {
        const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
        const glm::mat4 view = glm::lookAtRH(glm::vec3(0.0f, 2.0f, 5.0f),
                                             glm::vec3(0.0f, 0.0f, 0.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));

        const glm::vec3 worldPoints[] = {
            {0.0f, 0.0f, 0.0f}, {1.5f, -0.5f, -2.0f}, {-3.0f, 1.0f, 1.0f}, {0.25f, 0.75f, -4.0f},
        };

        for (const glm::vec3& p : worldPoints)
        {
            glm::vec4 viewPos = view * glm::vec4(p, 1.0f);
            glm::vec4 clip = proj * viewPos;
            REQUIRE(clip.w > 0.0f); // in front of the camera

            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            glm::vec2 uv = glm::vec2(ndc) * 0.5f + 0.5f;

            glm::vec3 recon = reconstructViewPos(proj, uv, ndc.z);
            CHECK(recon.x == doctest::Approx(viewPos.x).epsilon(0.001));
            CHECK(recon.y == doctest::Approx(viewPos.y).epsilon(0.001));
            CHECK(recon.z == doctest::Approx(viewPos.z).epsilon(0.001));
        }
    }

    TEST_CASE("Depth-collision reuses the shared bounce/friction response")
    {
        const glm::vec3 n(0.0f, 1.0f, 0.0f); // upward surface normal

        // Head-on into the surface, perfect restitution, no friction -> velocity reflects.
        {
            glm::vec3 v = collisionResponse(glm::vec3(0.0f, -2.0f, 0.0f), n, 0.0f, 1.0f);
            CHECK(v.y == doctest::Approx(2.0f));
            CHECK(v.x == doctest::Approx(0.0f));
            CHECK(v.z == doctest::Approx(0.0f));
        }

        // bounce = 0 removes the inward normal component (particle slides along the surface).
        {
            glm::vec3 v = collisionResponse(glm::vec3(3.0f, -2.0f, 0.0f), n, 0.0f, 0.0f);
            CHECK(v.y == doctest::Approx(0.0f));
            CHECK(v.x == doctest::Approx(3.0f)); // tangent preserved (friction 0)
        }

        // friction damps the tangent; bounce scales the reflected normal component.
        {
            glm::vec3 v = collisionResponse(glm::vec3(4.0f, -1.0f, 0.0f), n, 0.5f, 0.5f);
            CHECK(v.x == doctest::Approx(2.0f));  // 4 * (1 - 0.5)
            CHECK(v.y == doctest::Approx(0.5f));  // -(-1) * 0.5
        }

        // Moving away from the surface (vn >= 0) leaves velocity untouched.
        {
            glm::vec3 vIn(1.0f, 2.0f, 0.0f);
            glm::vec3 v = collisionResponse(vIn, n, 0.5f, 0.5f);
            CHECK(v.x == doctest::Approx(vIn.x));
            CHECK(v.y == doctest::Approx(vIn.y));
        }
    }
}
