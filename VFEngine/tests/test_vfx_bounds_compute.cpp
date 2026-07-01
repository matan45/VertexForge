// CPU-only coverage for the VK-1453 (Phase 4) analytic bounds helpers in
// VFXBoundsUtil.hpp:
//
//   * computeAutoBounds() mirrors the runtime frustum-cull heuristic — a
//     symmetric box of radius max(lifetime*startSpeed, maxShapeDim) + 5 centered
//     on the emitter origin.
//   * resolveBounds() returns the authored Fixed box when the mode is Fixed with
//     non-zero extents, and otherwise (Auto, or a degenerate zero-extent Fixed)
//     falls back to computeAutoBounds().
//
// Pure functions of VFXData — no render/GPU types, no Vulkan device.

#include <doctest.h>

#include <vfx/VFXBoundsUtil.hpp>
#include <vfx/VFXTypes.hpp>
#include <math/Frustum.hpp>

namespace
{
    void setFloatProp(vfx::VFXNode& node, const std::string& name, float value)
    {
        vfx::VFXProperty p;
        p.name = name;
        p.type = vfx::VFXPropertyType::Float;
        p.value = value;
        node.properties[name] = p;
    }

    void setStringProp(vfx::VFXNode& node, const std::string& name, const std::string& value)
    {
        vfx::VFXProperty p;
        p.name = name;
        p.type = vfx::VFXPropertyType::String;
        p.value = value;
        node.properties[name] = p;
    }

    // A single-emitter graph with explicit lifetime / startSpeed.
    vfx::VFXData makeEmitterVFX(float lifetime, float startSpeed)
    {
        vfx::VFXData data;
        vfx::VFXNode emitter;
        emitter.id = 1;
        emitter.type = vfx::VFXNodeType::Emitter;
        emitter.name = "Emitter";
        setFloatProp(emitter, "lifetime", lifetime);
        setFloatProp(emitter, "startSpeed", startSpeed);
        data.graph.nodes.push_back(emitter);
        data.graph.nextNodeId = 2;
        return data;
    }

    // Attach a Sphere shape (radius) to the emitter's "Shape" input pin so the
    // shape-config loader picks it up.
    void addSphereShape(vfx::VFXData& data, float radius)
    {
        const vfx::VFXNode* emitter = data.graph.findEmitterNode();
        REQUIRE(emitter != nullptr);
        const uint32_t emitterId = emitter->id;

        vfx::VFXNode shape;
        shape.id = 2;
        shape.type = vfx::VFXNodeType::Shape;
        shape.name = "Shape";
        setStringProp(shape, "shapeType", "Sphere");
        setFloatProp(shape, "radius", radius);
        data.graph.nodes.push_back(shape);
        data.graph.nextNodeId = 3;

        vfx::VFXNodeLink link;
        link.id = 1;
        link.sourceNodeId = 2;
        link.targetNodeId = emitterId;
        link.sourcePin = "Shape";
        link.targetPin = "Shape";
        data.graph.links.push_back(link);
        data.graph.nextLinkId = 2;
    }

    void checkSymmetricRadius(const math::AABB& box, float radius)
    {
        CHECK(box.min.x == doctest::Approx(-radius));
        CHECK(box.min.y == doctest::Approx(-radius));
        CHECK(box.min.z == doctest::Approx(-radius));
        CHECK(box.max.x == doctest::Approx(radius));
        CHECK(box.max.y == doctest::Approx(radius));
        CHECK(box.max.z == doctest::Approx(radius));
    }
}

TEST_SUITE("VFXBoundsCompute")
{
    TEST_CASE("auto bounds are driven by lifetime*startSpeed when no shape dominates")
    {
        // lifetime*startSpeed = 12; no shape node => maxShapeDim = 0.
        // radius = max(12, 0) + 5 = 17.
        const vfx::VFXData data = makeEmitterVFX(3.0f, 4.0f);
        checkSymmetricRadius(vfx::computeAutoBounds(data), 17.0f);
    }

    TEST_CASE("auto bounds are driven by the shape when it is the larger dimension")
    {
        // lifetime*startSpeed = 1; Sphere radius = 20 => maxShapeDim = 20.
        // radius = max(1, 20) + 5 = 25.
        vfx::VFXData data = makeEmitterVFX(1.0f, 1.0f);
        addSphereShape(data, 20.0f);
        checkSymmetricRadius(vfx::computeAutoBounds(data), 25.0f);
    }

    TEST_CASE("auto bounds fall back to emitter defaults when no emitter is present")
    {
        // No emitter node => defaults lifetime=2, startSpeed=1 => 2; no shape.
        // radius = max(2, 0) + 5 = 7.
        vfx::VFXData data;
        checkSymmetricRadius(vfx::computeAutoBounds(data), 7.0f);
    }

    TEST_CASE("resolveBounds returns the authored box for non-degenerate Fixed bounds")
    {
        const vfx::VFXData data = makeEmitterVFX(3.0f, 4.0f);

        vfx::VFXBounds bounds;
        bounds.mode = vfx::VFXBoundsMode::Fixed;
        bounds.center = glm::vec3(1.0f, 2.0f, 3.0f);
        bounds.extents = glm::vec3(4.0f, 5.0f, 6.0f);

        const math::AABB box = vfx::resolveBounds(bounds, data);
        CHECK(box.min.x == doctest::Approx(-3.0f));
        CHECK(box.min.y == doctest::Approx(-3.0f));
        CHECK(box.min.z == doctest::Approx(-3.0f));
        CHECK(box.max.x == doctest::Approx(5.0f));
        CHECK(box.max.y == doctest::Approx(7.0f));
        CHECK(box.max.z == doctest::Approx(9.0f));
    }

    TEST_CASE("resolveBounds falls back to auto for Auto mode")
    {
        const vfx::VFXData data = makeEmitterVFX(3.0f, 4.0f);

        vfx::VFXBounds bounds; // Auto, zero extents (default)
        const math::AABB resolved = vfx::resolveBounds(bounds, data);
        const math::AABB expected = vfx::computeAutoBounds(data);

        CHECK(resolved.min.x == doctest::Approx(expected.min.x));
        CHECK(resolved.max.x == doctest::Approx(expected.max.x));
        checkSymmetricRadius(resolved, 17.0f);
    }

    TEST_CASE("resolveBounds falls back to auto for a zero-extent Fixed box")
    {
        const vfx::VFXData data = makeEmitterVFX(3.0f, 4.0f);

        vfx::VFXBounds bounds;
        bounds.mode = vfx::VFXBoundsMode::Fixed;
        bounds.center = glm::vec3(10.0f, 10.0f, 10.0f);
        bounds.extents = glm::vec3(0.0f); // degenerate => auto fallback

        const math::AABB resolved = vfx::resolveBounds(bounds, data);
        checkSymmetricRadius(resolved, 17.0f);
    }
}
