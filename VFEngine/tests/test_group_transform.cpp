#include <doctest.h>
#include <windows/viewport/GroupTransformMath.hpp>
#include <windows/viewport/SceneEntityTransformUndo.hpp>
#include <impl/scene/TransformComponentService.hpp>
#include <impl/editor/UndoRedoServiceImpl.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <data/EntityConversion.hpp>
#include <events/EventDispatcher.hpp>
#include <events/scene/EntityTransformEvents.hpp>
#include <events/editor/UndoRedoEvents.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <memory>
#include <vector>

// ============================================================
// VK-1490: viewport group-transform math + scene-entity transform undo.
//
//  * grouptransform::worldDelta / applyWorldDelta — the pure pivot math the
//    gizmo uses for multi-selections (delta of the ACTIVE entity's world
//    matrix, re-applied to every top-level member's start world).
//  * SceneEntityTransformUndoCommand replayed through a dispatcher-registered
//    TransformComponentService (real EnTT registry + SceneGraphSystem).
//  * BeginBatch + N pushes + EndBatch collapse into ONE undo/redo operation.
// ============================================================

namespace
{
    constexpr float kEps = 1e-4f;

    bool approxVec(const glm::vec3& a, const glm::vec3& b, float eps = kEps)
    {
        return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps &&
               std::abs(a.z - b.z) < eps;
    }

    services::TransformData transformAt(const glm::vec3& position,
                                        const glm::vec3& rotation = glm::vec3(0.0f),
                                        const glm::vec3& scale = glm::vec3(1.0f))
    {
        services::TransformData t;
        t.position = position;
        t.rotation = rotation;
        t.scale = scale;
        return t;
    }

    services::TransformComponentService& transformService()
    {
        static auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        static services::TransformComponentService service{sceneGraph};
        static bool registered = false;
        if (!registered)
        {
            registered = true;
            service.registerEventHandlers(events::EventDispatcher::instance());
        }
        return service;
    }

    services::UndoRedoServiceImpl& undoService()
    {
        static services::UndoRedoServiceImpl service;
        static bool registered = false;
        if (!registered)
        {
            registered = true;
            service.registerEventHandlers();
        }
        return service;
    }

    struct TestEntities
    {
        entt::registry& registry = scene::EntityRegistry::getRegistry();
        std::vector<entt::entity> entities;

        services::EntityHandle createWithTransform(const glm::vec3& position)
        {
            entt::entity e = registry.create();
            entities.push_back(e);
            auto& t = registry.emplace<components::TransformComponent>(e);
            t.position = position;
            registry.emplace<components::WorldTransformComponent>(e).worldMatrix =
                glm::translate(glm::mat4(1.0f), position);
            return services::internal::toHandle(e);
        }

        void parent(services::EntityHandle child, services::EntityHandle parentHandle)
        {
            entt::entity c = services::internal::fromHandle(child);
            entt::entity p = services::internal::fromHandle(parentHandle);
            registry.emplace_or_replace<components::ParentComponent>(c).parent = p;
            auto* children = registry.try_get<components::ChildrenComponent>(p);
            if (!children)
            {
                children = &registry.emplace<components::ChildrenComponent>(p);
            }
            children->children.push_back(c);
        }

        ~TestEntities()
        {
            for (entt::entity e : entities)
            {
                if (registry.valid(e)) registry.destroy(e);
            }
        }
    };

    glm::vec3 localPositionOf(services::EntityHandle entity)
    {
        events::scene::GetTransformQuery query;
        query.entity = entity;
        auto t = events::EventDispatcher::instance().query(query);
        REQUIRE(t.has_value());
        return t->position;
    }
}

TEST_SUITE("GroupTransform") {

TEST_CASE("translation delta moves every member by the same offset") {
    const auto start = transformAt(glm::vec3(0.0f));
    const glm::mat4 startWorld = grouptransform::composeWorld(start);
    const glm::mat4 newWorld = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, -1.0f, 2.0f)) * startWorld;

    auto delta = grouptransform::worldDelta(startWorld, newWorld);
    REQUIRE(delta.has_value());

    const auto member = transformAt(glm::vec3(1.0f, 2.0f, 3.0f));
    auto moved = grouptransform::applyWorldDelta(*delta, member);
    REQUIRE(moved.has_value());
    CHECK(approxVec(moved->position, glm::vec3(6.0f, 1.0f, 5.0f)));
    CHECK(approxVec(moved->rotation, glm::vec3(0.0f)));
    CHECK(approxVec(moved->scale, glm::vec3(1.0f)));
}

TEST_CASE("rotation about the active pivot preserves relative offsets") {
    // Active (pivot) at the origin; 45-degree yaw (avoids the 90-degree XYZ
    // Euler-extraction singularity). A member at +X swings toward -Z on the
    // unit circle (right-handed Y rotation) while keeping its distance.
    const auto active = transformAt(glm::vec3(0.0f));
    const glm::mat4 startWorld = grouptransform::composeWorld(active);
    const glm::mat4 newWorld =
        grouptransform::composeWorld(transformAt(glm::vec3(0.0f), glm::vec3(0.0f, 45.0f, 0.0f)));

    auto delta = grouptransform::worldDelta(startWorld, newWorld);
    REQUIRE(delta.has_value());

    const float halfSqrt2 = 0.70710678f;
    const auto member = transformAt(glm::vec3(1.0f, 0.0f, 0.0f));
    auto moved = grouptransform::applyWorldDelta(*delta, member);
    REQUIRE(moved.has_value());
    CHECK(approxVec(moved->position, glm::vec3(halfSqrt2, 0.0f, -halfSqrt2)));
    CHECK(approxVec(moved->rotation, glm::vec3(0.0f, 45.0f, 0.0f)));
    CHECK(std::abs(glm::length(moved->position) - 1.0f) < kEps);
}

TEST_CASE("scale about the active pivot scales member offsets and size") {
    const auto active = transformAt(glm::vec3(0.0f));
    const glm::mat4 startWorld = grouptransform::composeWorld(active);
    const glm::mat4 newWorld =
        grouptransform::composeWorld(transformAt(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(2.0f)));

    auto delta = grouptransform::worldDelta(startWorld, newWorld);
    REQUIRE(delta.has_value());

    const auto member = transformAt(glm::vec3(1.0f, 2.0f, 3.0f));
    auto moved = grouptransform::applyWorldDelta(*delta, member);
    REQUIRE(moved.has_value());
    CHECK(approxVec(moved->position, glm::vec3(2.0f, 4.0f, 6.0f)));
    CHECK(approxVec(moved->scale, glm::vec3(2.0f)));
}

TEST_CASE("identity delta is a no-op") {
    const auto active = transformAt(glm::vec3(3.0f, 1.0f, -2.0f), glm::vec3(0.0f, 45.0f, 0.0f));
    const glm::mat4 startWorld = grouptransform::composeWorld(active);

    auto delta = grouptransform::worldDelta(startWorld, startWorld);
    REQUIRE(delta.has_value());

    const auto member = transformAt(glm::vec3(-4.0f, 0.5f, 7.0f), glm::vec3(10.0f, 20.0f, 30.0f));
    auto moved = grouptransform::applyWorldDelta(*delta, member);
    REQUIRE(moved.has_value());
    CHECK(approxVec(moved->position, member.position));
    CHECK(approxVec(moved->rotation, member.rotation));
    CHECK(approxVec(moved->scale, member.scale));
}

TEST_CASE("degenerate start scale yields no delta instead of NaN") {
    const glm::mat4 startWorld =
        grouptransform::composeWorld(transformAt(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f)));
    const glm::mat4 newWorld = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    CHECK_FALSE(grouptransform::worldDelta(startWorld, newWorld).has_value());
}

TEST_CASE("world transform component starts as identity") {
    const components::WorldTransformComponent world{};
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            const float expected = column == row ? 1.0f : 0.0f;
            CHECK(world.worldMatrix[column][row] == doctest::Approx(expected));
        }
    }
}

TEST_CASE("non-finite local transform is rejected") {
    TestEntities fixture;
    auto& service = transformService();
    const auto entity = fixture.createWithTransform(glm::vec3(1.0f, 2.0f, 3.0f));

    auto invalid = transformAt(glm::vec3(5.0f));
    invalid.position.x = std::numeric_limits<float>::quiet_NaN();
    service.setTransform(entity, invalid);

    CHECK(approxVec(localPositionOf(entity), glm::vec3(1.0f, 2.0f, 3.0f)));
}

TEST_CASE("singular parent world transform cannot corrupt child local transform") {
    TestEntities fixture;
    auto& service = transformService();
    const auto parent = fixture.createWithTransform(glm::vec3(0.0f));
    const auto child = fixture.createWithTransform(glm::vec3(2.0f, 3.0f, 4.0f));
    fixture.parent(child, parent);

    auto& parentWorld = fixture.registry.get<components::WorldTransformComponent>(
        services::internal::fromHandle(parent));
    parentWorld.worldMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(0.0f));

    service.setWorldTransform(child, transformAt(glm::vec3(10.0f, 20.0f, 30.0f)));

    CHECK(approxVec(localPositionOf(child), glm::vec3(2.0f, 3.0f, 4.0f)));
}

TEST_CASE("local-space undo command applies and reverts through the service") {
    TestEntities fixture;
    transformService();
    auto& dispatcher = events::EventDispatcher::instance();

    const auto entity = fixture.createWithTransform(glm::vec3(1.0f, 2.0f, 3.0f));
    const auto before = transformAt(glm::vec3(1.0f, 2.0f, 3.0f));
    const auto after = transformAt(glm::vec3(4.0f, 5.0f, 6.0f));

    windows::SceneEntityTransformUndoCommand command(entity, before, after,
                                                     /*worldSpace=*/false, "Transform Entity");
    command.execute();
    CHECK(approxVec(localPositionOf(entity), after.position));
    command.undo();
    CHECK(approxVec(localPositionOf(entity), before.position));
    (void)dispatcher;
}

TEST_CASE("world-space undo command converts world to local under a parent") {
    TestEntities fixture;
    transformService();

    const auto parent = fixture.createWithTransform(glm::vec3(10.0f, 0.0f, 0.0f));
    const auto child = fixture.createWithTransform(glm::vec3(0.0f));
    fixture.parent(child, parent);

    // World position 12 under a parent at 10 must land at local x == 2.
    const auto beforeWorld = transformAt(glm::vec3(10.0f, 0.0f, 0.0f));
    const auto afterWorld = transformAt(glm::vec3(12.0f, 0.0f, 0.0f));
    windows::SceneEntityTransformUndoCommand command(child, beforeWorld, afterWorld,
                                                     /*worldSpace=*/true, "Transform Entities");
    command.execute();
    CHECK(approxVec(localPositionOf(child), glm::vec3(2.0f, 0.0f, 0.0f)));
    command.undo();
    CHECK(approxVec(localPositionOf(child), glm::vec3(0.0f)));
}

TEST_CASE("undo replay on a dead entity is a safe no-op") {
    TestEntities fixture;
    transformService();

    const auto entity = fixture.createWithTransform(glm::vec3(0.0f));
    fixture.registry.destroy(services::internal::fromHandle(entity));

    windows::SceneEntityTransformUndoCommand command(
        entity, transformAt(glm::vec3(0.0f)), transformAt(glm::vec3(1.0f, 0.0f, 0.0f)),
        /*worldSpace=*/false, "Transform Entity");
    CHECK_NOTHROW(command.execute());
    CHECK_NOTHROW(command.undo());
}

TEST_CASE("a batched group drag is one undo/redo operation") {
    TestEntities fixture;
    transformService();
    undoService();
    auto& dispatcher = events::EventDispatcher::instance();

    const glm::vec3 offsets[3] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    const glm::vec3 moved[3] = {{5.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f}, {5.0f, 1.0f, 0.0f}};

    services::EntityHandle entities[3];
    for (int i = 0; i < 3; ++i)
    {
        entities[i] = fixture.createWithTransform(offsets[i]);
    }

    // Simulate the drag result, then record it exactly as finalizeDrag does.
    for (int i = 0; i < 3; ++i)
    {
        events::scene::SetTransformCommand set;
        set.entity = entities[i];
        set.transform = transformAt(moved[i]);
        dispatcher.execute(set);
    }

    events::undoredo::BeginBatchCommand begin;
    begin.description = "Transform Entities";
    dispatcher.execute(begin);
    for (int i = 0; i < 3; ++i)
    {
        events::undoredo::PushUndoableCommand push;
        push.command = std::make_shared<windows::SceneEntityTransformUndoCommand>(
            entities[i], transformAt(offsets[i]), transformAt(moved[i]),
            /*worldSpace=*/false, "Transform Entities");
        dispatcher.execute(push);
    }
    dispatcher.execute(events::undoredo::EndBatchCommand{});

    // ONE undo restores all three members.
    CHECK(dispatcher.execute(events::undoredo::UndoCommand{}));
    for (int i = 0; i < 3; ++i)
    {
        CHECK(approxVec(localPositionOf(entities[i]), offsets[i]));
    }

    // ONE redo re-applies all three.
    CHECK(dispatcher.execute(events::undoredo::RedoCommand{}));
    for (int i = 0; i < 3; ++i)
    {
        CHECK(approxVec(localPositionOf(entities[i]), moved[i]));
    }

    // Clean the shared undo stacks so later cases in other TUs are unaffected.
    dispatcher.execute(events::undoredo::UndoCommand{});
}

} // TEST_SUITE
