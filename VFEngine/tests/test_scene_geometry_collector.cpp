#include <doctest.h>
#include <impl/navigation/SceneGeometryCollector.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <cmath>
#include <vector>

// ============================================================
// VK-1660: SceneGeometryCollector - navigation bake geometry is gathered in WORLD space (a parented
// obstacle used to bake at its local transform), and moving entities never contribute.
// ============================================================

namespace
{
    constexpr float kEps = 1e-3f;

    // Far from the origin, so geometry other suites leave in the global registry stays out of the bounds.
    const glm::vec3 kSite{6000.0f, 0.0f, 6000.0f};

    bool vecApprox(const glm::vec3& a, const glm::vec3& b, float eps = kEps)
    {
        return std::abs(a.x - b.x) <= eps && std::abs(a.y - b.y) <= eps && std::abs(a.z - b.z) <= eps;
    }

    // Entities in the global registry, destroyed when the scope ends.
    struct ScopedEntities
    {
        std::vector<entt::entity> entities;

        ScopedEntities() { scene::EntityRegistry::init(); }

        ~ScopedEntities()
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            for (auto entity : entities)
            {
                if (registry.valid(entity))
                    registry.destroy(entity);
            }
        }

        entt::entity make(const glm::vec3& position, const glm::vec3& rotation = glm::vec3(0.0f))
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            const entt::entity entity = registry.create();
            auto& transform = registry.emplace<components::TransformComponent>(entity);
            transform.position = position;
            transform.rotation = rotation;
            entities.push_back(entity);
            return entity;
        }
    };

    void setParent(entt::entity child, entt::entity parent)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        registry.emplace_or_replace<components::ParentComponent>(child, components::ParentComponent{parent});
        registry.get_or_emplace<components::ChildrenComponent>(parent).children.push_back(child);
    }

    void addBoxCollider(entt::entity entity, const glm::vec3& size, bool trigger = false)
    {
        auto& collider = scene::EntityRegistry::getRegistry().emplace<components::ColliderComponent>(entity);
        collider.shape = components::ColliderShape::Box;
        collider.size = size;
        collider.isTrigger = trigger;
    }

    services::SceneGeometrySources only(bool colliders, bool obstacles)
    {
        services::SceneGeometrySources sources;
        sources.terrain = false;
        sources.staticMeshes = false;
        sources.foliage = false;
        sources.colliders = colliders;
        sources.obstacles = obstacles;
        return sources;
    }

    navigation::NavmeshTileBounds boundsAround(const glm::vec3& center, float halfSize)
    {
        navigation::NavmeshTileBounds bounds;
        bounds.min = center - glm::vec3(halfSize);
        bounds.max = center + glm::vec3(halfSize);
        return bounds;
    }

    glm::vec3 centroid(const navigation::NavmeshInputGeometry& geometry)
    {
        glm::vec3 sum(0.0f);
        const int count = geometry.getVertexCount();
        for (int i = 0; i < count; ++i)
        {
            sum += glm::vec3(geometry.vertices[i * 3], geometry.vertices[i * 3 + 1], geometry.vertices[i * 3 + 2]);
        }
        return count > 0 ? sum / static_cast<float>(count) : sum;
    }
}

TEST_SUITE("SceneGeometryCollector")
{
    TEST_CASE("a parented box collider is collected at its world position")
    {
        ScopedEntities scope;
        const entt::entity group = scope.make(kSite, glm::vec3(0.0f, 90.0f, 0.0f));
        const entt::entity wall = scope.make(glm::vec3(10.0f, 0.0f, 0.0f));
        setParent(wall, group);
        addBoxCollider(wall, glm::vec3(2.0f));

        navigation::NavmeshInputGeometry geometry;
        services::SceneGeometryCollector(only(true, false)).collect(boundsAround(kSite, 100.0f), geometry);

        REQUIRE(geometry.getVertexCount() == 8);
        CHECK(geometry.getTriangleCount() == 12);
        // The parent's 90 degree yaw turns the child's local +X offset into world -Z.
        CHECK(vecApprox(centroid(geometry), kSite + glm::vec3(0.0f, 0.0f, -10.0f)));
    }

    TEST_CASE("an unparented collider is collected at its own transform")
    {
        ScopedEntities scope;
        const entt::entity block = scope.make(kSite + glm::vec3(20.0f, 1.0f, 0.0f));
        addBoxCollider(block, glm::vec3(1.0f));

        navigation::NavmeshInputGeometry geometry;
        services::SceneGeometryCollector(only(true, false)).collect(boundsAround(kSite, 100.0f), geometry);

        REQUIRE(geometry.getVertexCount() == 8);
        CHECK(vecApprox(centroid(geometry), kSite + glm::vec3(20.0f, 1.0f, 0.0f)));
    }

    TEST_CASE("bounds match entities by their world origin")
    {
        ScopedEntities scope;
        const services::SceneGeometryCollector collector(only(true, false));

        // Whatever other suites left near the origin.
        navigation::NavmeshInputGeometry baseline;
        collector.collect(boundsAround(glm::vec3(0.0f), 10.0f), baseline);

        const entt::entity group = scope.make(kSite);
        const entt::entity wall = scope.make(glm::vec3(0.0f));
        setParent(wall, group);
        addBoxCollider(wall, glm::vec3(2.0f));

        // The wall's local origin (0,0,0) is inside these bounds, its world origin is not.
        navigation::NavmeshInputGeometry atLocalOrigin;
        collector.collect(boundsAround(glm::vec3(0.0f), 10.0f), atLocalOrigin);
        CHECK(atLocalOrigin.getVertexCount() == baseline.getVertexCount());

        navigation::NavmeshInputGeometry atSite;
        collector.collect(boundsAround(kSite, 10.0f), atSite);
        CHECK(atSite.getVertexCount() == 8);
    }

    TEST_CASE("movers and triggers are never collected")
    {
        ScopedEntities scope;
        auto& registry = scene::EntityRegistry::getRegistry();

        const entt::entity agent = scope.make(kSite);
        addBoxCollider(agent, glm::vec3(1.0f));
        registry.emplace<components::NavmeshAgentComponent>(agent);

        const entt::entity flyer = scope.make(kSite);
        addBoxCollider(flyer, glm::vec3(1.0f));
        registry.emplace<components::VolumetricAgentComponent>(flyer);

        const entt::entity player = scope.make(kSite);
        addBoxCollider(player, glm::vec3(1.0f));
        registry.emplace<components::ControllerComponent>(player);

        const entt::entity crate = scope.make(kSite);
        addBoxCollider(crate, glm::vec3(1.0f));
        registry.emplace<components::RigidBodyComponent>(crate).type = components::RigidBodyType::Dynamic;

        const entt::entity trigger = scope.make(kSite);
        addBoxCollider(trigger, glm::vec3(1.0f), /*trigger=*/true);

        const entt::entity pillar = scope.make(kSite + glm::vec3(3.0f, 0.0f, 0.0f));
        addBoxCollider(pillar, glm::vec3(1.0f));
        registry.emplace<components::RigidBodyComponent>(pillar).type = components::RigidBodyType::Static;

        navigation::NavmeshInputGeometry geometry;
        services::SceneGeometryCollector(only(true, false)).collect(boundsAround(kSite, 100.0f), geometry);

        REQUIRE(geometry.getVertexCount() == 8); // the static pillar only
        CHECK(vecApprox(centroid(geometry), kSite + glm::vec3(3.0f, 0.0f, 0.0f)));
    }

    TEST_CASE("a parented carve obstacle is collected at its world position; avoidance-only is not")
    {
        ScopedEntities scope;
        auto& registry = scene::EntityRegistry::getRegistry();

        const entt::entity group = scope.make(kSite + glm::vec3(0.0f, 5.0f, 0.0f));
        const entt::entity barrier = scope.make(glm::vec3(0.0f, 0.0f, 3.0f));
        setParent(barrier, group);
        auto& carve = registry.emplace<components::NavmeshObstacleComponent>(barrier);
        carve.mode = components::NavmeshObstacleMode::Carve;
        carve.shape = components::NavmeshObstacleShape::Box;
        carve.size = glm::vec3(1.0f, 2.0f, 1.0f);

        const entt::entity post = scope.make(kSite);
        auto& avoid = registry.emplace<components::NavmeshObstacleComponent>(post);
        avoid.mode = components::NavmeshObstacleMode::AvoidanceOnly;

        navigation::NavmeshInputGeometry geometry;
        services::SceneGeometryCollector(only(false, true)).collect(boundsAround(kSite, 100.0f), geometry);

        REQUIRE(geometry.getVertexCount() == 8);
        CHECK(vecApprox(centroid(geometry), kSite + glm::vec3(0.0f, 5.0f, 3.0f)));
    }

    TEST_CASE("a disabled source contributes nothing")
    {
        ScopedEntities scope;
        const entt::entity block = scope.make(kSite);
        addBoxCollider(block, glm::vec3(1.0f));

        navigation::NavmeshInputGeometry geometry;
        services::SceneGeometryCollector(only(false, false)).collect(boundsAround(kSite, 100.0f), geometry);
        CHECK(geometry.isEmpty());
    }
}
