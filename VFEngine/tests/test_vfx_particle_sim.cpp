#include <doctest.h>

#include <render/vfx/particle/VFXParticleSystem.hpp>
#include <threading/JobSystem.hpp>
#include <vfx/VFXForceTypes.hpp>
#include <vfx/VFXModifierTypes.hpp>
#include <vfx/VFXTypes.hpp>

#include <glm/glm.hpp>

#include <cmath>

// VK-1476 review fixes: these lock in CPU-preview <-> GPU-compute-shader parity for the
// VFXParticleSystem simulation (the editor preview must match the shipped GPU runtime,
// resources/shaders/vfx/vfx_particle_sim.glsl):
//   - updateParticle now runs forces -> integrate -> modifiers (GPU main() order), so
//     SpeedOverLifetime rescales the POST-force velocity and preserves its direction.
//   - the default end-of-life courtesy fade is gated on the GPU `modifierFlags != 0u`
//     equivalent (any modifier OR any force OR a flipbook flag), not modifiers alone.

namespace
{
    struct JobSystemScope
    {
        JobSystemScope() { threading::JobSystem::instance().init(); }
        ~JobSystemScope() { threading::JobSystem::instance().shutdown(); }
    };

    // One-particle emitter: a single burst of 1 at t=0 with no continuous spawn, so exactly
    // one particle exists for the whole run and its physics can be tracked (mirrors
    // test_vfx_forces.cpp).
    render::vfx::VFXEmitterConfig singleParticleConfig(float startSpeed)
    {
        render::vfx::VFXEmitterConfig config;
        config.spawnRate = 0.0f;
        config.looping = false;
        config.lifetime = 1000.0f;
        config.startSize = 1.0f;
        config.startSpeed = startSpeed;
        config.emitDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        config.speedVariance = 0.0f;
        config.sizeVariance = 0.0f;
        config.lifetimeVariance = 0.0f;
        config.alphaVariance = 0.0f;
        config.colorValueVariance = 0.0f;
        config.shape.type = ::vfx::ShapeType::Point;

        ::vfx::VFXBurst burst;
        burst.time = 0.0f;
        burst.count = 1;
        burst.cycles = 1;
        config.bursts.push_back(burst);
        return config;
    }

    const render::vfx::VFXParticle* firstActive(const render::vfx::VFXParticleSystem& system)
    {
        for (const auto& p : system.getParticles())
            if (p.active)
                return &p;
        return nullptr;
    }
}

TEST_SUITE("VFXParticleSimParity")
{
    // Locks A1 (forces -> integrate -> modifiers order) + A2 (SpeedOverLifetime preserves the
    // force-deflected direction). Old code snapped velocity back onto the spawn direction every
    // frame, so a force could never bend a SpeedOverLifetime particle in the CPU preview.
    TEST_CASE("SpeedOverLifetime preserves force-deflected direction")
    {
        JobSystemScope jobs;

        render::vfx::VFXEmitterConfig config = singleParticleConfig(10.0f);
        config.emitDirection = glm::vec3(0.0f, 1.0f, 0.0f);

        // Constant speed curve: magnitude stays pinned to initialSpeed, so only DIRECTION can
        // change - isolating the direction-preservation behavior.
        ::vfx::SpeedOverLifetimeConfig speedMod;
        speedMod.curve = ::vfx::VFXCurve::constant(1.0f);
        config.modifiers.modifiers.push_back(speedMod);

        // Strong lateral force, perpendicular to the +Y launch: it should bend the path toward +X.
        ::vfx::GravityForceConfig gravity;
        gravity.direction = glm::vec3(1.0f, 0.0f, 0.0f);
        gravity.strength = 20.0f;
        config.forces.forces.push_back(gravity);

        render::vfx::VFXParticleSystem system;
        system.setSeed(4242);
        system.setEmitterConfig(config);
        system.setPlaying(true);

        system.update(0.05f); // spawn frame (the new particle is not updated this frame)
        const render::vfx::VFXParticle* spawned = firstActive(system);
        REQUIRE(spawned != nullptr);
        const glm::vec3 v0 = spawned->velocity; // ~ +Y * 10
        REQUIRE(glm::length(v0) == doctest::Approx(10.0f));

        for (int i = 0; i < 50; ++i)
            system.update(0.05f);

        const render::vfx::VFXParticle* pp = firstActive(system);
        REQUIRE(pp != nullptr);

        // Direction must have deflected toward the force. With the old order/reset it would stay
        // aligned with the spawn direction (align ~= 1).
        const float align = glm::dot(glm::normalize(pp->velocity), glm::normalize(v0));
        CHECK(align < 0.8f);
        CHECK(pp->velocity.x > 4.0f);                                  // bent substantially toward +X
        CHECK(glm::length(pp->velocity) == doctest::Approx(10.0f));    // constant curve pins the speed
    }

    // Locks A1 fade-gate: the CPU default courtesy fade must match GPU `config.modifierFlags != 0u`,
    // which the host OR's from any modifier, any force, and the flipbook flags - so a forces-only
    // emitter must NOT fade (it does not on the GPU), while a bare emitter still fades.
    TEST_CASE("default end-of-life fade matches the GPU modifierFlags gate")
    {
        JobSystemScope jobs;
        const float dt = 0.1f;

        auto alphaNearEndOfLife = [dt](bool addForce) -> float {
            render::vfx::VFXEmitterConfig config = singleParticleConfig(0.0f);
            config.lifetime = 1.0f;
            config.startColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            if (addForce)
            {
                ::vfx::GravityForceConfig gravity;
                gravity.direction = glm::vec3(0.0f, -1.0f, 0.0f);
                gravity.strength = 1.0f;
                config.forces.forces.push_back(gravity);
            }

            render::vfx::VFXParticleSystem system;
            system.setSeed(9);
            system.setEmitterConfig(config);
            system.setPlaying(true);

            system.update(dt); // spawn (lifetime = 0)
            for (int i = 0; i < 100; ++i)
            {
                const render::vfx::VFXParticle* p = firstActive(system);
                if (!p || p->lifetime / p->maxLifetime >= 0.8f)
                    break;
                system.update(dt);
            }

            const render::vfx::VFXParticle* p = firstActive(system);
            REQUIRE(p != nullptr);
            REQUIRE(p->lifetime / p->maxLifetime > 0.7f); // inside the fade window
            REQUIRE(p->lifetime < p->maxLifetime);        // still alive
            return p->color.a;
        };

        // Bare emitter (no modifiers, no forces): modifierFlags == 0 -> courtesy fade runs.
        CHECK(alphaNearEndOfLife(false) < 0.9f);
        // Forces-only emitter: modifierFlags != 0 -> fade suppressed, alpha stays full.
        CHECK(alphaNearEndOfLife(true) == doctest::Approx(1.0f));
    }
}
