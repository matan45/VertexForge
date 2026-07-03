#include <doctest.h>

#include <render/vfx/particle/VFXParticleSystem.hpp>
#include <threading/JobSystem.hpp>
#include <vfx/VFXCurlNoise.hpp>
#include <vfx/VFXForceConfigLoader.hpp>
#include <vfx/VFXForceTypes.hpp>
#include <vfx/VFXTypes.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string>

// VK-1465: Drag and Point Attractor forces. These tests exercise the CPU particle-sim
// mirror (VFXParticleSystem) that shares its force math conceptually with
// resources/shaders/vfx/vfx_particle_sim.glsl, plus the graph->config loader round-trip.

namespace
{
    struct JobSystemScope
    {
        JobSystemScope() { threading::JobSystem::instance().init(); }
        ~JobSystemScope() { threading::JobSystem::instance().shutdown(); }
    };

    // A one-particle emitter: a single burst of 1 at t=0 with no continuous spawn, so
    // exactly one particle exists for the whole run and its physics can be tracked.
    render::vfx::VFXEmitterConfig singleParticleConfig(float startSpeed)
    {
        render::vfx::VFXEmitterConfig config;
        config.spawnRate = 0.0f;
        config.looping = false;
        config.lifetime = 1000.0f;      // long: the particle survives the whole test
        config.startSize = 1.0f;
        config.startSpeed = startSpeed;
        config.emitDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        config.speedVariance = 0.0f;
        config.sizeVariance = 0.0f;
        config.lifetimeVariance = 0.0f;
        config.shape.type = ::vfx::ShapeType::Point; // spawn at origin

        ::vfx::VFXBurst burst;
        burst.time = 0.0f;
        burst.count = 1;
        burst.cycles = 1;
        config.bursts.push_back(burst);
        return config;
    }

    // Returns a copy of the single active particle (there is exactly one in these tests).
    // Re-queried each frame so no pointer is held across update() calls.
    const render::vfx::VFXParticle* firstActive(const render::vfx::VFXParticleSystem& system)
    {
        for (const auto& p : system.getParticles())
            if (p.active)
                return &p;
        return nullptr;
    }

    // --- Loader helpers (mirror test_vfx_force_shape_properties.cpp) ------------------

    vfx::VFXNode makeNode(uint32_t id, vfx::VFXNodeType type, const std::string& name)
    {
        vfx::VFXNode node;
        node.id = id;
        node.type = type;
        node.name = name;
        return node;
    }

    void setFloat(vfx::VFXNode& node, const std::string& name, float value, float min = 0.0f, float max = 1.0f)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Float, value, min, max};
    }

    void setBool(vfx::VFXNode& node, const std::string& name, bool value)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Bool, value, 0.0f, 1.0f};
    }

    void setInt(vfx::VFXNode& node, const std::string& name, int value, float min = 0.0f, float max = 10.0f)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Int, value, min, max};
    }

    void setVec3(vfx::VFXNode& node, const std::string& name, const glm::vec3& value,
                 float min = -100.0f, float max = 100.0f)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Vec3, value, min, max};
    }

    vfx::VFXNodeLink makeLink(uint32_t id, uint32_t source, uint32_t target, const std::string& targetPin = "Input")
    {
        vfx::VFXNodeLink link;
        link.id = id;
        link.sourceNodeId = source;
        link.targetNodeId = target;
        link.sourcePin = "Output";
        link.targetPin = targetPin;
        return link;
    }
}

TEST_SUITE("VFXForces")
{
    TEST_CASE("drag monotonically reduces speed without reversing direction")
    {
        JobSystemScope jobs;

        render::vfx::VFXEmitterConfig config = singleParticleConfig(10.0f);
        ::vfx::DragForceConfig drag;
        drag.linearCoeff = 2.0f;
        drag.quadraticCoeff = 0.0f;
        config.forces.forces.push_back(drag);

        render::vfx::VFXParticleSystem system;
        system.setSeed(1234);
        system.setEmitterConfig(config);
        system.setPlaying(true);

        system.update(0.1f); // update #1 spawns the burst particle (no forces applied yet)
        const render::vfx::VFXParticle* p = firstActive(system);
        REQUIRE(p != nullptr);

        const glm::vec3 v0 = p->velocity;
        const float speed0 = glm::length(v0);
        CHECK(speed0 == doctest::Approx(10.0f)); // startSpeed, no variance

        float prevSpeed = speed0;
        glm::vec3 prevVelocity = v0;
        for (int i = 0; i < 50; ++i)
        {
            system.update(0.1f);
            const render::vfx::VFXParticle* pp = firstActive(system);
            REQUIRE(pp != nullptr);

            const float speed = glm::length(pp->velocity);
            CHECK(std::isfinite(speed));
            CHECK(speed < prevSpeed);                        // strictly decreasing
            CHECK(glm::dot(pp->velocity, prevVelocity) > 0.0f); // never reverses

            prevSpeed = speed;
            prevVelocity = pp->velocity;
        }

        CHECK(prevSpeed < speed0 * 0.5f); // converges toward zero
    }

    TEST_CASE("drag never reverses velocity at large dt")
    {
        JobSystemScope jobs;

        render::vfx::VFXEmitterConfig config = singleParticleConfig(10.0f);
        ::vfx::DragForceConfig drag;
        drag.linearCoeff = 5.0f; // k*dt = 5*10 = 50 >> 1; explicit v -= k*v*dt would overshoot/reverse
        config.forces.forces.push_back(drag);

        render::vfx::VFXParticleSystem system;
        system.setSeed(7);
        system.setEmitterConfig(config);
        system.setPlaying(true);

        system.update(0.01f);
        const render::vfx::VFXParticle* p = firstActive(system);
        REQUIRE(p != nullptr);
        const glm::vec3 v0 = p->velocity;

        system.update(10.0f); // huge step
        const render::vfx::VFXParticle* pp = firstActive(system);
        REQUIRE(pp != nullptr);

        CHECK(std::isfinite(glm::length(pp->velocity)));
        CHECK(glm::length(pp->velocity) < glm::length(v0)); // damped
        CHECK(glm::dot(pp->velocity, v0) > 0.0f);           // same direction, never reversed
    }

    TEST_CASE("point attractor pulls the particle toward the center and stays bounded")
    {
        JobSystemScope jobs;

        render::vfx::VFXEmitterConfig config = singleParticleConfig(0.0f); // spawn at rest
        ::vfx::PointAttractorForceConfig attractor;
        attractor.position = glm::vec3(10.0f, 0.0f, 0.0f);
        attractor.strength = 5.0f;
        attractor.radius = 100.0f;
        attractor.falloff = 1.0f;
        config.forces.forces.push_back(attractor);

        render::vfx::VFXParticleSystem system;
        system.setSeed(99);
        system.setEmitterConfig(config);
        system.setPlaying(true);

        system.update(0.05f);
        const render::vfx::VFXParticle* p = firstActive(system);
        REQUIRE(p != nullptr);

        const glm::vec3 center = attractor.position;
        const float d0 = glm::length(center - p->position);
        CHECK(d0 == doctest::Approx(10.0f));                 // spawned at origin
        CHECK(glm::length(p->velocity) == doctest::Approx(0.0f)); // at rest

        float minDist = d0;
        float maxDist = d0;
        bool decreasedEarly = false;
        for (int i = 0; i < 200; ++i)
        {
            system.update(0.05f);
            const render::vfx::VFXParticle* pp = firstActive(system);
            REQUIRE(pp != nullptr);

            const float d = glm::length(center - pp->position);
            CHECK(std::isfinite(d));
            CHECK(std::isfinite(glm::length(pp->velocity)));

            minDist = std::min(minDist, d);
            maxDist = std::max(maxDist, d);
            if (i < 20 && d < d0)
                decreasedEarly = true;
        }

        CHECK(decreasedEarly);          // moved toward the center early
        CHECK(minDist < d0 * 0.2f);     // reaches the center (undamped oscillator passes through)
        CHECK(maxDist < d0 * 2.0f);     // symplectic integrator: bounded, no energy blow-up
    }

    TEST_CASE("point attractor with drag settles the particle near the center")
    {
        JobSystemScope jobs;

        render::vfx::VFXEmitterConfig config = singleParticleConfig(0.0f);
        ::vfx::PointAttractorForceConfig attractor;
        attractor.position = glm::vec3(10.0f, 0.0f, 0.0f);
        attractor.strength = 5.0f;
        attractor.radius = 100.0f;
        attractor.falloff = 1.0f;
        config.forces.forces.push_back(attractor);

        ::vfx::DragForceConfig drag;
        drag.linearCoeff = 1.0f;
        config.forces.forces.push_back(drag);

        render::vfx::VFXParticleSystem system;
        system.setSeed(5);
        system.setEmitterConfig(config);
        system.setPlaying(true);

        system.update(0.05f);
        const render::vfx::VFXParticle* p = firstActive(system);
        REQUIRE(p != nullptr);
        const glm::vec3 center = attractor.position;
        const float d0 = glm::length(center - p->position);

        for (int i = 0; i < 400; ++i)
            system.update(0.05f);

        const render::vfx::VFXParticle* pp = firstActive(system);
        REQUIRE(pp != nullptr);
        const float dFinal = glm::length(center - pp->position);
        CHECK(dFinal < d0 * 0.2f);              // damped oscillation converges to the center
        CHECK(glm::length(pp->velocity) < 1.0f); // slowed down
    }

    TEST_CASE("point attractor with negative strength repels the particle")
    {
        JobSystemScope jobs;

        render::vfx::VFXEmitterConfig config = singleParticleConfig(0.0f);
        ::vfx::PointAttractorForceConfig repulsor;
        repulsor.position = glm::vec3(10.0f, 0.0f, 0.0f);
        repulsor.strength = -5.0f; // negative = repel
        repulsor.radius = 100.0f;
        repulsor.falloff = 1.0f;
        config.forces.forces.push_back(repulsor);

        render::vfx::VFXParticleSystem system;
        system.setSeed(3);
        system.setEmitterConfig(config);
        system.setPlaying(true);

        system.update(0.05f);
        const render::vfx::VFXParticle* p = firstActive(system);
        REQUIRE(p != nullptr);
        const glm::vec3 center = repulsor.position;
        const float d0 = glm::length(center - p->position);

        for (int i = 0; i < 20; ++i)
            system.update(0.05f);

        const render::vfx::VFXParticle* pp = firstActive(system);
        REQUIRE(pp != nullptr);
        CHECK(glm::length(center - pp->position) > d0); // pushed away
    }

    TEST_CASE("point attractor killAtCenter removes the particle at the center")
    {
        JobSystemScope jobs;

        render::vfx::VFXEmitterConfig config = singleParticleConfig(0.0f);
        ::vfx::PointAttractorForceConfig attractor;
        attractor.position = glm::vec3(5.0f, 0.0f, 0.0f);
        attractor.strength = 8.0f;
        attractor.radius = 10.0f;   // killRadius = max(0.05, 10*0.05) = 0.5 (genuinely near center)
        attractor.falloff = 1.0f;
        attractor.killAtCenter = true;
        config.forces.forces.push_back(attractor);

        render::vfx::VFXParticleSystem system;
        system.setSeed(11);
        system.setEmitterConfig(config);
        system.setPlaying(true);

        system.update(0.02f);
        REQUIRE(firstActive(system) != nullptr); // spawned

        bool killed = false;
        for (int i = 0; i < 500; ++i)
        {
            system.update(0.02f);
            if (firstActive(system) == nullptr)
            {
                killed = true;
                break;
            }
        }
        CHECK(killed);
    }

    TEST_CASE("force loader round-trips drag and point attractor configs")
    {
        vfx::VFXGraph graph;
        graph.nodes.push_back(makeNode(1, vfx::VFXNodeType::Emitter, "Emitter"));

        vfx::VFXNode drag = makeNode(2, vfx::VFXNodeType::ForceDrag, "Drag");
        setFloat(drag, "linearCoeff", 2.5f, 0.0f, 10.0f);
        setFloat(drag, "quadraticCoeff", 0.75f, 0.0f, 10.0f);
        setBool(drag, "localSpace", true);
        graph.nodes.push_back(drag);

        vfx::VFXNode attractor = makeNode(3, vfx::VFXNodeType::ForcePointAttractor, "Point Attractor");
        setVec3(attractor, "position", glm::vec3(4.0f, 5.0f, 6.0f));
        setFloat(attractor, "strength", -3.5f, -50.0f, 50.0f);
        setFloat(attractor, "radius", 12.0f, 0.0f, 100.0f);
        setFloat(attractor, "falloff", 2.0f, 0.0f, 10.0f);
        setBool(attractor, "killAtCenter", true);
        setBool(attractor, "localSpace", false);
        graph.nodes.push_back(attractor);

        graph.nodes.push_back(makeNode(4, vfx::VFXNodeType::OutSystem, "Output"));
        graph.nodes.push_back(makeNode(5, vfx::VFXNodeType::Shape, "Shape"));
        graph.links.push_back(makeLink(1, 1, 5, "Shape"));
        graph.links.push_back(makeLink(2, 1, 2));
        graph.links.push_back(makeLink(3, 2, 3));
        graph.links.push_back(makeLink(4, 3, 4));
        graph.nextNodeId = 6;
        graph.nextLinkId = 5;

        const vfx::VFXForceChain chain = vfx::VFXForceConfigLoader::fromGraph(graph);
        REQUIRE(chain.forces.size() == 2);

        const auto& dragCfg = std::get<vfx::DragForceConfig>(chain.forces[0]);
        CHECK(dragCfg.linearCoeff == doctest::Approx(2.5f));
        CHECK(dragCfg.quadraticCoeff == doctest::Approx(0.75f));
        CHECK(dragCfg.space == vfx::ForceSpace::Local);

        const auto& attrCfg = std::get<vfx::PointAttractorForceConfig>(chain.forces[1]);
        CHECK(attrCfg.position.x == doctest::Approx(4.0f));
        CHECK(attrCfg.position.y == doctest::Approx(5.0f));
        CHECK(attrCfg.position.z == doctest::Approx(6.0f));
        CHECK(attrCfg.strength == doctest::Approx(-3.5f));
        CHECK(attrCfg.radius == doctest::Approx(12.0f));
        CHECK(attrCfg.falloff == doctest::Approx(2.0f));
        CHECK(attrCfg.killAtCenter == true);
        CHECK(attrCfg.space == vfx::ForceSpace::World);
    }

    // --- VK-1466 Curl Noise ----------------------------------------------------------

    TEST_CASE("curl noise velocity field is numerically divergence-free")
    {
        // F = curl(Psi) is divergence-free by construction. Measured with central
        // differences at the SAME step the curl uses (and frequency == 1 so the world
        // step matches the internal q-space step), the discrete divergence telescopes
        // to float rounding — expect ~1e-5..1e-4, comfortably below the epsilon.
        const float h = vfx::kCurlEpsilon;
        const float inv2h = 1.0f / (2.0f * h);
        const glm::vec3 dx(h, 0.0f, 0.0f), dy(0.0f, h, 0.0f), dz(0.0f, 0.0f, h);

        auto F = [&](const glm::vec3& p) {
            return vfx::evalCurlNoise(1.0f, 1.0f, 0.0f, 1, p, 0.0f); // strength=freq=1, no scroll
        };

        float maxDiv = 0.0f;
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 5; ++j)
                for (int k = 0; k < 5; ++k)
                {
                    // Offset off the integer lattice so we sample generic interior points.
                    const glm::vec3 p(0.3f + 0.7f * i, 0.3f + 0.7f * j, 0.3f + 0.7f * k);
                    const float div = (F(p + dx).x - F(p - dx).x) * inv2h +
                                      (F(p + dy).y - F(p - dy).y) * inv2h +
                                      (F(p + dz).z - F(p - dz).z) * inv2h;
                    CHECK(std::isfinite(div));
                    maxDiv = std::max(maxDiv, std::abs(div));
                }

        CHECK(maxDiv < 1e-2f);
    }

    TEST_CASE("curl noise is deterministic and pure")
    {
        // No RNG, no time source, no statics: identical args -> bit-identical result.
        for (int i = 0; i < 8; ++i)
        {
            const glm::vec3 p(0.13f * i, 1.7f - 0.2f * i, -0.5f + 0.4f * i);
            const glm::vec3 a = vfx::evalCurlNoise(2.0f, 1.5f, 0.3f, 2, p, 1.25f);
            const glm::vec3 b = vfx::evalCurlNoise(2.0f, 1.5f, 0.3f, 2, p, 1.25f);
            CHECK(a.x == b.x);
            CHECK(a.y == b.y);
            CHECK(a.z == b.z);
        }
    }

    TEST_CASE("curl noise produces a live, non-constant rotational field")
    {
        float maxMag = 0.0f;
        glm::vec3 sum(0.0f), sumSq(0.0f);
        int n = 0;
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 5; ++j)
                for (int k = 0; k < 5; ++k)
                {
                    const glm::vec3 p(0.3f + 0.7f * i, 0.3f + 0.7f * j, 0.3f + 0.7f * k);
                    const glm::vec3 f = vfx::evalCurlNoise(1.0f, 1.0f, 0.0f, 1, p, 0.0f);
                    maxMag = std::max(maxMag, glm::length(f));
                    sum += f;
                    sumSq += f * f;
                    ++n;
                }

        const glm::vec3 mean = sum / static_cast<float>(n);
        const glm::vec3 var = sumSq / static_cast<float>(n) - mean * mean;
        CHECK(maxMag > 0.05f);                          // field is alive
        CHECK(var.x + var.y + var.z > 1e-3f);           // not a constant field
    }

    TEST_CASE("force loader round-trips a curl noise config")
    {
        vfx::VFXGraph graph;
        graph.nodes.push_back(makeNode(1, vfx::VFXNodeType::Emitter, "Emitter"));

        vfx::VFXNode curl = makeNode(2, vfx::VFXNodeType::ForceCurlNoise, "Curl Noise");
        setFloat(curl, "strength", 3.5f, 0.0f, 50.0f);
        setFloat(curl, "frequency", 0.6f, 0.1f, 10.0f);
        setFloat(curl, "scrollSpeed", 0.25f, 0.0f, 10.0f);
        setInt(curl, "octaves", 3, 1.0f, 4.0f);
        setBool(curl, "localSpace", true);
        graph.nodes.push_back(curl);

        graph.nodes.push_back(makeNode(3, vfx::VFXNodeType::OutSystem, "Output"));
        graph.nodes.push_back(makeNode(4, vfx::VFXNodeType::Shape, "Shape"));
        graph.links.push_back(makeLink(1, 1, 4, "Shape"));
        graph.links.push_back(makeLink(2, 1, 2));
        graph.links.push_back(makeLink(3, 2, 3));
        graph.nextNodeId = 5;
        graph.nextLinkId = 4;

        const vfx::VFXForceChain chain = vfx::VFXForceConfigLoader::fromGraph(graph);
        REQUIRE(chain.forces.size() == 1);

        const auto& cfg = std::get<vfx::CurlNoiseForceConfig>(chain.forces[0]);
        CHECK(cfg.strength == doctest::Approx(3.5f));
        CHECK(cfg.frequency == doctest::Approx(0.6f));
        CHECK(cfg.scrollSpeed == doctest::Approx(0.25f));
        CHECK(cfg.octaves == 3);
        CHECK(cfg.space == vfx::ForceSpace::Local);
    }

    TEST_CASE("curl noise loader clamps octaves to [1,4]")
    {
        auto octavesFor = [](int authored) {
            vfx::VFXGraph graph;
            graph.nodes.push_back(makeNode(1, vfx::VFXNodeType::Emitter, "Emitter"));
            vfx::VFXNode curl = makeNode(2, vfx::VFXNodeType::ForceCurlNoise, "Curl Noise");
            setInt(curl, "octaves", authored, 0.0f, 100.0f);
            graph.nodes.push_back(curl);
            graph.nodes.push_back(makeNode(3, vfx::VFXNodeType::OutSystem, "Output"));
            graph.nodes.push_back(makeNode(4, vfx::VFXNodeType::Shape, "Shape"));
            graph.links.push_back(makeLink(1, 1, 4, "Shape"));
            graph.links.push_back(makeLink(2, 1, 2));
            graph.links.push_back(makeLink(3, 2, 3));
            const vfx::VFXForceChain chain = vfx::VFXForceConfigLoader::fromGraph(graph);
            REQUIRE(chain.forces.size() == 1);
            return std::get<vfx::CurlNoiseForceConfig>(chain.forces[0]).octaves;
        };

        CHECK(octavesFor(100) == 4);
        CHECK(octavesFor(0) == 1);
        CHECK(octavesFor(2) == 2);
    }

    TEST_CASE("curl noise imparts finite non-zero velocity to a particle")
    {
        JobSystemScope jobs;

        render::vfx::VFXEmitterConfig config = singleParticleConfig(0.0f); // spawn at rest
        ::vfx::CurlNoiseForceConfig curl;
        curl.strength = 5.0f;
        curl.frequency = 1.0f;
        curl.scrollSpeed = 0.0f;
        curl.octaves = 1;
        config.forces.forces.push_back(curl);

        render::vfx::VFXParticleSystem system;
        system.setSeed(42);
        system.setEmitterConfig(config);
        system.setPlaying(true);

        system.update(0.05f); // spawns the burst particle
        REQUIRE(firstActive(system) != nullptr);

        for (int i = 0; i < 100; ++i)
            system.update(0.05f);

        const render::vfx::VFXParticle* pp = firstActive(system);
        REQUIRE(pp != nullptr);
        CHECK(std::isfinite(glm::length(pp->velocity)));
        CHECK(glm::length(pp->velocity) > 1e-4f); // curl advected the particle
    }
}
