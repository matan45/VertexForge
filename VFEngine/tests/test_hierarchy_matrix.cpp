#include <doctest.h>
#include <scene/HierarchyMatrix.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <cmath>

// ============================================================
// VK-1660: scene::composeWorldMatrix (HierarchyMatrix.hpp) - the parent-chain world matrix the
// navmesh / volumetric geometry collectors use instead of the LOCAL transform.
// ============================================================

namespace
{
    constexpr float kEps = 1e-4f;

    bool matApprox(const glm::mat4& a, const glm::mat4& b, float eps = kEps)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (std::abs(a[column][row] - b[column][row]) > eps)
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool vecApprox(const glm::vec3& a, const glm::vec3& b, float eps = kEps)
    {
        return std::abs(a.x - b.x) <= eps && std::abs(a.y - b.y) <= eps && std::abs(a.z - b.z) <= eps;
    }

    void setTransform(entt::registry& registry, entt::entity entity,
                      const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale)
    {
        auto& transform = registry.get_or_emplace<components::TransformComponent>(entity);
        transform.position = position;
        transform.rotation = rotation;
        transform.scale = scale;
    }

    glm::mat4 localOf(const entt::registry& registry, entt::entity entity)
    {
        return registry.get<components::TransformComponent>(entity).getMatrix();
    }
}

TEST_SUITE("HierarchyMatrix")
{
    TEST_CASE("three-level chain composes parent * child * grandchild")
    {
        entt::registry registry;
        const entt::entity top = registry.create();
        const entt::entity middle = registry.create();
        const entt::entity leaf = registry.create();

        setTransform(registry, top, glm::vec3(10.0f, 0.0f, -4.0f), glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(2.0f));
        setTransform(registry, middle, glm::vec3(0.0f, 5.0f, 1.0f), glm::vec3(30.0f, 0.0f, 15.0f),
                     glm::vec3(1.0f, 0.5f, 1.0f));
        setTransform(registry, leaf, glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0.0f, 0.0f, 45.0f),
                     glm::vec3(0.5f, 1.0f, 1.5f));
        registry.emplace<components::ParentComponent>(middle).parent = top;
        registry.emplace<components::ParentComponent>(leaf).parent = middle;

        const glm::mat4 expected = localOf(registry, top) * localOf(registry, middle) * localOf(registry, leaf);
        CHECK(matApprox(scene::composeWorldMatrix(registry, leaf), expected));
        CHECK(vecApprox(scene::composeWorldPosition(registry, leaf), glm::vec3(expected[3])));

        CHECK(matApprox(scene::composeWorldMatrix(registry, middle),
                        localOf(registry, top) * localOf(registry, middle)));
        CHECK(matApprox(scene::composeWorldMatrix(registry, top), localOf(registry, top)));
    }

    TEST_CASE("an entity without a transform counts as identity")
    {
        entt::registry registry;
        const entt::entity top = registry.create();
        const entt::entity group = registry.create(); // no TransformComponent
        const entt::entity leaf = registry.create();

        setTransform(registry, top, glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(0.0f, 45.0f, 0.0f), glm::vec3(1.0f));
        setTransform(registry, leaf, glm::vec3(0.0f, 1.0f, 2.0f), glm::vec3(0.0f), glm::vec3(1.0f));
        registry.emplace<components::ParentComponent>(group).parent = top;
        registry.emplace<components::ParentComponent>(leaf).parent = group;

        CHECK(matApprox(scene::composeWorldMatrix(registry, leaf), localOf(registry, top) * localOf(registry, leaf)));
        CHECK(matApprox(scene::composeWorldMatrix(registry, group), localOf(registry, top)));

        // A parent handle that no longer exists ends the walk.
        const entt::entity orphan = registry.create();
        setTransform(registry, orphan, glm::vec3(7.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f));
        const entt::entity gone = registry.create();
        registry.emplace<components::ParentComponent>(orphan).parent = gone;
        registry.destroy(gone);
        CHECK(vecApprox(scene::composeWorldPosition(registry, orphan), glm::vec3(7.0f, 0.0f, 0.0f)));
    }

    TEST_CASE("parent cycles terminate")
    {
        entt::registry registry;
        const entt::entity self = registry.create();
        setTransform(registry, self, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f));
        registry.emplace<components::ParentComponent>(self).parent = self;
        CHECK(matApprox(scene::composeWorldMatrix(registry, self), glm::mat4(1.0f)));

        const entt::entity a = registry.create();
        const entt::entity b = registry.create();
        setTransform(registry, a, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f));
        setTransform(registry, b, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f));
        registry.emplace<components::ParentComponent>(a).parent = b;
        registry.emplace<components::ParentComponent>(b).parent = a;
        CHECK(matApprox(scene::composeWorldMatrix(registry, a), glm::mat4(1.0f)));
    }

    TEST_CASE("matches SceneGraphSystem world transforms")
    {
        scene::SceneGraphSystem graph;
        scene::Entity top("HierarchyMatrixTop");
        scene::Entity middle("HierarchyMatrixMiddle");
        scene::Entity leaf("HierarchyMatrixLeaf");
        graph.addChild(graph.GetRoot(), top);
        graph.addChild(top, middle);
        graph.addChild(middle, leaf);

        auto& registry = scene::EntityRegistry::getRegistry();
        setTransform(registry, top.getHandle(), glm::vec3(-6.0f, 2.0f, 9.0f), glm::vec3(10.0f, 60.0f, 0.0f),
                     glm::vec3(1.5f));
        setTransform(registry, middle.getHandle(), glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(0.0f, -30.0f, 20.0f),
                     glm::vec3(1.0f, 2.0f, 1.0f));
        setTransform(registry, leaf.getHandle(), glm::vec3(0.0f, 1.0f, -3.0f), glm::vec3(45.0f, 0.0f, 0.0f),
                     glm::vec3(0.75f));
        graph.markTransformDirty(top);
        graph.updateWorldTransforms();

        REQUIRE(leaf.hasComponent<components::WorldTransformComponent>());
        CHECK(matApprox(scene::composeWorldMatrix(registry, leaf.getHandle()),
                        leaf.getComponent<components::WorldTransformComponent>().worldMatrix));
        CHECK(matApprox(scene::composeWorldMatrix(registry, middle.getHandle()),
                        middle.getComponent<components::WorldTransformComponent>().worldMatrix));

        graph.removeEntity(top);
        registry.destroy(graph.GetRoot().getHandle());
    }
}
