#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include <render/vfx/compute/VFXColorTint.hpp>
#include <render/vfx/lut/VFXLUTBaker.hpp>
#include <render/vfx/particle/VFXParticleSystem.hpp>
#include <threading/JobSystem.hpp>
#include <vfx/VFXModifierTypes.hpp>

#include <glm/glm.hpp>

#include <cstring>

// VK-1657: the per-instance colour tint (the "colorTint" override) multiplies every authored colour
// the particle sim reads — the GPU config's start colour and colour-over-lifetime endpoints, the
// colour LUT channel, and the CPU fallback sim's spawn colour / gradient. White is byte-identical,
// and tinting the freshly rebuilt data each frame never compounds.

namespace
{
    namespace GPUC = render::vfx::GPUVFXConstants;
    namespace LUTCh = render::vfx::LUTChannel;
    namespace LUTFl = render::vfx::LUTFlags;

    struct JobSystemScope
    {
        JobSystemScope() { threading::JobSystem::instance().init(); }
        ~JobSystemScope() { threading::JobSystem::instance().shutdown(); }
    };

    render::vfx::GPUEmitterConfig sampleConfig()
    {
        render::vfx::GPUEmitterConfig config{};
        config.startColor = glm::vec4(1.0f, 0.5f, 0.25f, 1.0f);
        config.colorStart = glm::vec4(1.0f, 0.8f, 0.4f, 1.0f);
        config.colorEnd = glm::vec4(0.5f, 0.5f, 0.5f, 0.0f);
        config.spawnRate = 40.0f;
        return config;
    }

    ::vfx::VFXModifierChain colorOverLifetimeChain()
    {
        ::vfx::ColorOverLifetimeConfig colorOverLifetime;
        colorOverLifetime.gradient = ::vfx::VFXGradient::fromStartEnd(
            glm::vec4(1.0f, 0.8f, 0.4f, 1.0f), glm::vec4(0.2f, 0.2f, 0.2f, 0.0f));
        ::vfx::SizeOverLifetimeConfig sizeOverLifetime;

        ::vfx::VFXModifierChain chain;
        chain.modifiers.push_back(colorOverLifetime);
        chain.modifiers.push_back(sizeOverLifetime);
        return chain;
    }

    // One particle from a single burst, no continuous spawn and no variance (mirrors
    // test_vfx_particle_sim.cpp), so its colour is exactly the authored colour times the tint.
    render::vfx::VFXEmitterConfig singleParticleConfig()
    {
        render::vfx::VFXEmitterConfig config;
        config.spawnRate = 0.0f;
        config.looping = false;
        config.lifetime = 10.0f;
        config.startSize = 1.0f;
        config.startSpeed = 0.0f;
        config.startColor = glm::vec4(1.0f, 0.5f, 0.25f, 1.0f);
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

TEST_SUITE("VFXColorTint")
{
    TEST_CASE("a white tint leaves the GPU config byte-identical")
    {
        const render::vfx::GPUEmitterConfig before = sampleConfig();
        render::vfx::GPUEmitterConfig after = before;
        render::vfx::applyColorTint(after, glm::vec4(1.0f));
        CHECK(std::memcmp(&before, &after, sizeof(render::vfx::GPUEmitterConfig)) == 0);
    }

    TEST_CASE("the tint multiplies the start colour and both over-lifetime endpoints only")
    {
        render::vfx::GPUEmitterConfig config = sampleConfig();
        const glm::vec4 tint(1.0f, 0.5f, 2.0f, 0.5f);
        render::vfx::applyColorTint(config, tint);

        CHECK(config.startColor.y == doctest::Approx(0.25f));
        CHECK(config.startColor.z == doctest::Approx(0.5f));
        CHECK(config.startColor.w == doctest::Approx(0.5f));
        CHECK(config.colorStart.y == doctest::Approx(0.4f));
        CHECK(config.colorStart.z == doctest::Approx(0.8f));
        CHECK(config.colorEnd.x == doctest::Approx(0.5f));
        CHECK(config.colorEnd.w == doctest::Approx(0.0f));
        CHECK(config.spawnRate == doctest::Approx(40.0f));
    }

    TEST_CASE("the LUT tint touches the colour channel only, and only when it is live")
    {
        const ::vfx::VFXModifierChain chain = colorOverLifetimeChain();
        const render::vfx::LUTBakeResult baked = render::vfx::VFXLUTBaker::bake(chain);
        REQUIRE((baked.lutFlags & LUTFl::Color) != 0u);
        REQUIRE(baked.data.size() >= 2 * GPUC::LUT_RESOLUTION);

        render::vfx::LUTBakeResult tinted = baked;
        const glm::vec4 tint(1.0f, 0.5f, 0.25f, 1.0f);
        render::vfx::tintLUTColor(tinted, tint);

        const size_t colorBegin = static_cast<size_t>(LUTCh::Color) * GPUC::LUT_RESOLUTION;
        for (size_t i = 0; i < baked.data.size(); ++i)
        {
            const bool colorTexel = i >= colorBegin && i < colorBegin + GPUC::LUT_RESOLUTION;
            const glm::vec4 expected = colorTexel ? baked.data[i] * tint : baked.data[i];
            CHECK(tinted.data[i].x == doctest::Approx(expected.x));
            CHECK(tinted.data[i].y == doctest::Approx(expected.y));
            CHECK(tinted.data[i].z == doctest::Approx(expected.z));
            CHECK(tinted.data[i].w == doctest::Approx(expected.w));
        }

        // No colour modifier: the colour flag is off, so the sim never samples channel 0 — untouched.
        ::vfx::VFXModifierChain sizeOnly;
        sizeOnly.modifiers.push_back(::vfx::SizeOverLifetimeConfig{});
        const render::vfx::LUTBakeResult noColor = render::vfx::VFXLUTBaker::bake(sizeOnly);
        REQUIRE((noColor.lutFlags & LUTFl::Color) == 0u);
        render::vfx::LUTBakeResult untouched = noColor;
        render::vfx::tintLUTColor(untouched, tint);
        REQUIRE(untouched.data.size() == noColor.data.size());
        for (size_t i = 0; i < noColor.data.size(); ++i)
        {
            CHECK(untouched.data[i] == noColor.data[i]);
        }
    }

    TEST_CASE("tinting each frame's fresh bake gives the same result every frame")
    {
        const ::vfx::VFXModifierChain chain = colorOverLifetimeChain();
        const glm::vec4 tint(1.0f, 0.6f, 0.25f, 1.0f);

        render::vfx::LUTBakeResult frame1 = render::vfx::VFXLUTBaker::bake(chain);
        render::vfx::tintLUTColor(frame1, tint);
        render::vfx::LUTBakeResult frame2 = render::vfx::VFXLUTBaker::bake(chain);
        render::vfx::tintLUTColor(frame2, tint);

        REQUIRE(frame1.data.size() == frame2.data.size());
        for (size_t i = 0; i < frame1.data.size(); ++i)
        {
            CHECK(frame1.data[i] == frame2.data[i]);
        }

        // A white tint keeps the bake as is.
        const render::vfx::LUTBakeResult plain = render::vfx::VFXLUTBaker::bake(chain);
        render::vfx::LUTBakeResult white = plain;
        render::vfx::tintLUTColor(white, glm::vec4(1.0f));
        for (size_t i = 0; i < plain.data.size(); ++i)
        {
            CHECK(white.data[i] == plain.data[i]);
        }
    }

    TEST_CASE("the CPU fallback sim spawns with the tinted start colour")
    {
        JobSystemScope jobs;

        render::vfx::VFXEmitterConfig config = singleParticleConfig();
        config.colorTint = glm::vec4(0.5f, 1.0f, 2.0f, 1.0f);

        render::vfx::VFXParticleSystem system;
        system.setSeed(7);
        system.setEmitterConfig(config);
        system.setPlaying(true);
        system.update(0.05f); // spawn frame: the new particle is not updated yet

        const render::vfx::VFXParticle* particle = firstActive(system);
        REQUIRE(particle != nullptr);
        CHECK(particle->color.x == doctest::Approx(0.5f));
        CHECK(particle->color.y == doctest::Approx(0.5f));
        CHECK(particle->color.z == doctest::Approx(0.5f));
        CHECK(particle->color.w == doctest::Approx(1.0f));
    }

    TEST_CASE("the CPU fallback sim tints the colour-over-lifetime gradient")
    {
        JobSystemScope jobs;

        render::vfx::VFXEmitterConfig config = singleParticleConfig();
        ::vfx::ColorOverLifetimeConfig colorOverLifetime;
        colorOverLifetime.gradient = ::vfx::VFXGradient::fromStartEnd(glm::vec4(1.0f), glm::vec4(1.0f));
        config.modifiers.modifiers.push_back(colorOverLifetime);
        config.colorTint = glm::vec4(1.0f, 0.5f, 0.25f, 1.0f);

        render::vfx::VFXParticleSystem system;
        system.setSeed(7);
        system.setEmitterConfig(config);
        system.setPlaying(true);
        system.update(0.05f); // spawn
        system.update(0.05f); // the modifier runs on the live particle

        const render::vfx::VFXParticle* particle = firstActive(system);
        REQUIRE(particle != nullptr);
        // A constant white gradient times the tint is the tint, whatever the particle's age.
        CHECK(particle->color.x == doctest::Approx(1.0f));
        CHECK(particle->color.y == doctest::Approx(0.5f));
        CHECK(particle->color.z == doctest::Approx(0.25f));
    }
}
