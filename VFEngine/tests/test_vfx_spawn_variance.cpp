#include <doctest.h>

#include <render/vfx/particle/VFXParticleSystem.hpp>
#include <threading/JobSystem.hpp>
#include <vfx/VFXAsset.hpp>
#include <vfx/VFXEmitterConfigLoader.hpp>
#include <vfx/VFXVariance.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    struct JobSystemScope
    {
        JobSystemScope() { threading::JobSystem::instance().init(); }
        ~JobSystemScope() { threading::JobSystem::instance().shutdown(); }
    };

    void setFloat(vfx::VFXNode& node, const std::string& name, float value, float min = 0.0f, float max = 1.0f)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Float, value, min, max};
    }

    std::vector<render::vfx::VFXParticle> activeParticles(const render::vfx::VFXParticleSystem& system)
    {
        std::vector<render::vfx::VFXParticle> out;
        for (const auto& particle : system.getParticles())
        {
            if (particle.active)
                out.push_back(particle);
        }
        return out;
    }

    render::vfx::VFXEmitterConfig baseConfig(float spawnRate)
    {
        render::vfx::VFXEmitterConfig config;
        config.spawnRate = spawnRate;
        config.lifetime = 4.0f;
        config.startSize = 2.0f;
        config.startSpeed = 3.0f;
        config.startColor = glm::vec4(1.0f);
        config.looping = true;
        return config;
    }

    std::vector<render::vfx::VFXParticle> spawnOnce(const render::vfx::VFXEmitterConfig& config, uint32_t seed)
    {
        render::vfx::VFXParticleSystem system;
        system.setSeed(seed);
        system.setEmitterConfig(config);
        system.setPlaying(true);
        system.update(1.0f);
        return activeParticles(system);
    }

    fs::path varianceTestRoot()
    {
        return fs::temp_directory_path() / "vf_vfx_spawn_variance_tests";
    }

    void resetVarianceTestRoot()
    {
        std::error_code ec;
        fs::remove_all(varianceTestRoot(), ec);
        fs::create_directories(varianceTestRoot(), ec);
    }

    std::string readFileText(const fs::path& path)
    {
        std::ifstream in(path);
        REQUIRE(in.is_open());
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
}

TEST_SUITE("VFXSpawnVariance")
{
    TEST_CASE("variance hash is deterministic bounded and stream-separated")
    {
        const uint32_t seed = 0x12345678u;
        const float a = vfx::vfxVarianceSigned(seed, vfx::VarianceStream::Size);
        const float b = vfx::vfxVarianceSigned(seed, vfx::VarianceStream::Size);
        const float c = vfx::vfxVarianceSigned(seed, vfx::VarianceStream::Lifetime);

        CHECK(a == doctest::Approx(b));
        CHECK(a >= -1.0f);
        CHECK(a <= 1.0f);
        CHECK(c >= -1.0f);
        CHECK(c <= 1.0f);
        CHECK(a != doctest::Approx(c));
    }

    TEST_CASE("loader reads variance keys clamps fractions and converts degrees")
    {
        vfx::VFXData data = vfx::VFXAsset::createDefault("variance_loader");
        vfx::VFXNode* emitter = data.graph.findNode(1);
        REQUIRE(emitter != nullptr);

        setFloat(*emitter, "sizeVariance", 1.25f);
        setFloat(*emitter, "lifetimeVariance", -0.25f);
        setFloat(*emitter, "speedVariance", 0.5f);
        setFloat(*emitter, "rotationVariance", 90.0f, 0.0f, 180.0f);
        setFloat(*emitter, "angularVelocityVariance", 360.0f, 0.0f, 720.0f);
        setFloat(*emitter, "colorValueVariance", 0.35f);
        setFloat(*emitter, "alphaVariance", 2.0f);

        const render::vfx::VFXEmitterConfig config = vfx::VFXEmitterConfigLoader::fromVFXData(data);
        CHECK(config.sizeVariance == doctest::Approx(1.0f));
        CHECK(config.lifetimeVariance == doctest::Approx(0.0f));
        CHECK(config.speedVariance == doctest::Approx(0.5f));
        CHECK(config.rotationVariance == doctest::Approx(glm::radians(90.0f)));
        CHECK(config.angularVelocityVariance == doctest::Approx(glm::radians(360.0f)));
        CHECK(config.colorValueVariance == doctest::Approx(0.35f));
        CHECK(config.alphaVariance == doctest::Approx(1.0f));

        vfx::VFXData missing;
        vfx::VFXNode missingEmitter;
        missingEmitter.id = 1;
        missingEmitter.type = vfx::VFXNodeType::Emitter;
        missingEmitter.name = "Emitter";
        missing.graph.nodes.push_back(missingEmitter);

        const render::vfx::VFXEmitterConfig defaults = vfx::VFXEmitterConfigLoader::fromVFXData(missing);
        CHECK(defaults.sizeVariance == doctest::Approx(0.0f));
        CHECK(defaults.lifetimeVariance == doctest::Approx(0.0f));
        CHECK(defaults.speedVariance == doctest::Approx(0.0f));
        CHECK(defaults.rotationVariance == doctest::Approx(0.0f));
        CHECK(defaults.angularVelocityVariance == doctest::Approx(0.0f));
        CHECK(defaults.colorValueVariance == doctest::Approx(0.0f));
        CHECK(defaults.alphaVariance == doctest::Approx(0.0f));
    }

    TEST_CASE(".vfVFX round-trips spawn variance properties without changing format version")
    {
        resetVarianceTestRoot();
        vfx::VFXData data = vfx::VFXAsset::createDefault("variance_roundtrip");
        vfx::VFXNode* emitter = data.graph.findNode(1);
        REQUIRE(emitter != nullptr);

        setFloat(*emitter, "sizeVariance", 0.1f);
        setFloat(*emitter, "lifetimeVariance", 0.2f);
        setFloat(*emitter, "speedVariance", 0.3f);
        setFloat(*emitter, "rotationVariance", 45.0f, 0.0f, 180.0f);
        setFloat(*emitter, "angularVelocityVariance", 180.0f, 0.0f, 720.0f);
        setFloat(*emitter, "colorValueVariance", 0.4f);
        setFloat(*emitter, "alphaVariance", 0.5f);

        const fs::path path = varianceTestRoot() / "Variance.vfVFX";
        REQUIRE(vfx::VFXAsset::save(path.string(), data));
        CHECK(readFileText(path).find("\"version\": \"1.1\"") != std::string::npos);

        auto loaded = vfx::VFXAsset::load(path.string());
        REQUIRE(loaded.has_value());
        const auto config = vfx::VFXEmitterConfigLoader::fromVFXData(*loaded);
        CHECK(config.sizeVariance == doctest::Approx(0.1f));
        CHECK(config.lifetimeVariance == doctest::Approx(0.2f));
        CHECK(config.speedVariance == doctest::Approx(0.3f));
        CHECK(config.rotationVariance == doctest::Approx(glm::radians(45.0f)));
        CHECK(config.angularVelocityVariance == doctest::Approx(glm::radians(180.0f)));
        CHECK(config.colorValueVariance == doctest::Approx(0.4f));
        CHECK(config.alphaVariance == doctest::Approx(0.5f));
    }

    TEST_CASE("zero variance preserves spawned baseline attributes and position stream")
    {
        JobSystemScope jobs;
        const auto config = baseConfig(16.0f);

        const auto baseline = spawnOnce(config, 1234u);
        auto zeroConfig = config;
        zeroConfig.sizeVariance = 0.0f;
        zeroConfig.lifetimeVariance = 0.0f;
        zeroConfig.speedVariance = 0.0f;
        zeroConfig.rotationVariance = 0.0f;
        zeroConfig.angularVelocityVariance = 0.0f;
        zeroConfig.colorValueVariance = 0.0f;
        zeroConfig.alphaVariance = 0.0f;
        const auto zero = spawnOnce(zeroConfig, 1234u);

        REQUIRE(baseline.size() == zero.size());
        REQUIRE(zero.size() == 16);
        for (size_t i = 0; i < zero.size(); ++i)
        {
            CHECK(zero[i].position.x == doctest::Approx(baseline[i].position.x));
            CHECK(zero[i].position.y == doctest::Approx(baseline[i].position.y));
            CHECK(zero[i].position.z == doctest::Approx(baseline[i].position.z));
            CHECK(zero[i].size == doctest::Approx(config.startSize));
            CHECK(zero[i].initialSize == doctest::Approx(config.startSize));
            CHECK(zero[i].initialSpeed == doctest::Approx(config.startSpeed));
            CHECK(zero[i].maxLifetime == doctest::Approx(config.lifetime));
            CHECK(zero[i].rotation == doctest::Approx(0.0f));
            CHECK(zero[i].angularVelocity == doctest::Approx(0.0f));
            CHECK(zero[i].color.r == doctest::Approx(config.startColor.r));
            CHECK(zero[i].color.g == doctest::Approx(config.startColor.g));
            CHECK(zero[i].color.b == doctest::Approx(config.startColor.b));
            CHECK(zero[i].color.a == doctest::Approx(config.startColor.a));
        }
    }

    TEST_CASE("nonzero variance stays in authored ranges and produces spread")
    {
        JobSystemScope jobs;
        auto config = baseConfig(128.0f);
        config.sizeVariance = 0.5f;
        config.lifetimeVariance = 0.5f;
        config.speedVariance = 0.5f;
        config.rotationVariance = glm::radians(90.0f);
        config.angularVelocityVariance = glm::radians(180.0f);
        config.colorValueVariance = 0.5f;
        config.alphaVariance = 0.5f;

        const auto particles = spawnOnce(config, 777u);
        REQUIRE(particles.size() == 128);

        float minSize = std::numeric_limits<float>::max();
        float maxSize = std::numeric_limits<float>::lowest();
        float minSpeed = std::numeric_limits<float>::max();
        float maxSpeed = std::numeric_limits<float>::lowest();
        float minRotation = std::numeric_limits<float>::max();
        float maxRotation = std::numeric_limits<float>::lowest();
        float minRed = std::numeric_limits<float>::max();
        float maxRed = std::numeric_limits<float>::lowest();

        for (const auto& p : particles)
        {
            const float speed = glm::length(p.velocity);
            CHECK(p.size >= config.startSize * 0.5f);
            CHECK(p.size <= config.startSize * 1.5f);
            CHECK(p.maxLifetime >= config.lifetime * 0.5f);
            CHECK(p.maxLifetime <= config.lifetime * 1.5f);
            CHECK(speed >= config.startSpeed * 0.5f);
            CHECK(speed <= config.startSpeed * 1.5f);
            CHECK(p.rotation >= -config.rotationVariance);
            CHECK(p.rotation <= config.rotationVariance);
            CHECK(p.angularVelocity >= -config.angularVelocityVariance);
            CHECK(p.angularVelocity <= config.angularVelocityVariance);
            CHECK(p.color.r >= 0.5f);
            CHECK(p.color.r <= 1.5f);
            CHECK(p.color.a >= 0.5f);
            CHECK(p.color.a <= 1.5f);

            minSize = std::min(minSize, p.size);
            maxSize = std::max(maxSize, p.size);
            minSpeed = std::min(minSpeed, speed);
            maxSpeed = std::max(maxSpeed, speed);
            minRotation = std::min(minRotation, p.rotation);
            maxRotation = std::max(maxRotation, p.rotation);
            minRed = std::min(minRed, p.color.r);
            maxRed = std::max(maxRed, p.color.r);
        }

        CHECK(minSize < maxSize);
        CHECK(minSpeed < maxSpeed);
        CHECK(minRotation < maxRotation);
        CHECK(minRed < maxRed);
    }

    TEST_CASE("angular velocity advances rotation after spawn")
    {
        JobSystemScope jobs;
        auto config = baseConfig(1.0f);
        config.rotationVariance = glm::radians(45.0f);
        config.angularVelocityVariance = glm::radians(360.0f);

        render::vfx::VFXParticleSystem system;
        system.setSeed(99u);
        system.setEmitterConfig(config);
        system.setPlaying(true);
        system.update(1.0f);

        auto particles = activeParticles(system);
        REQUIRE(particles.size() == 1);
        const float initialRotation = particles[0].rotation;
        const float angularVelocity = particles[0].angularVelocity;
        CHECK(std::abs(angularVelocity) > 1.0e-6f);

        config.spawnRate = 0.0f;
        system.setEmitterConfig(config);
        system.update(0.25f);

        particles = activeParticles(system);
        REQUIRE(particles.size() == 1);
        CHECK(particles[0].rotation == doctest::Approx(initialRotation + angularVelocity * 0.25f));
    }

    TEST_CASE("fixed seed and config produce identical variance sequences")
    {
        JobSystemScope jobs;
        auto config = baseConfig(32.0f);
        config.sizeVariance = 0.4f;
        config.lifetimeVariance = 0.25f;
        config.speedVariance = 0.3f;
        config.rotationVariance = glm::radians(120.0f);
        config.angularVelocityVariance = glm::radians(240.0f);
        config.colorValueVariance = 0.2f;
        config.alphaVariance = 0.3f;

        const auto a = spawnOnce(config, 2026u);
        const auto b = spawnOnce(config, 2026u);

        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            CHECK(a[i].size == doctest::Approx(b[i].size));
            CHECK(a[i].maxLifetime == doctest::Approx(b[i].maxLifetime));
            CHECK(a[i].initialSpeed == doctest::Approx(b[i].initialSpeed));
            CHECK(a[i].rotation == doctest::Approx(b[i].rotation));
            CHECK(a[i].angularVelocity == doctest::Approx(b[i].angularVelocity));
            CHECK(a[i].color.r == doctest::Approx(b[i].color.r));
            CHECK(a[i].color.a == doctest::Approx(b[i].color.a));
        }
    }
}
