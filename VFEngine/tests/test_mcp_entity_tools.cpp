#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    struct DispatcherScope
    {
        DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }

        ~DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }
    };

    services::EntityHandle handleOf(uint64_t id)
    {
        services::EntityHandle handle;
        handle.id = id;
        return handle;
    }

    services::EntityData entityData(uint64_t id, std::string name, std::optional<uint64_t> parent,
                                    std::vector<uint64_t> children = {})
    {
        services::EntityData data;
        data.handle = handleOf(id);
        data.name = std::move(name);
        if (parent.has_value())
        {
            data.parent = handleOf(*parent);
        }
        for (uint64_t child : children)
        {
            data.children.push_back(handleOf(child));
        }
        return data;
    }

    // Fake scene: root 100 -> {1 "Floor", 2 "Player" -> {3 "Camera"}}. Newly
    // created entities get id 7.
    struct FakeScene
    {
        static constexpr uint64_t rootId = 100;
        static constexpr uint64_t createdId = 7;

        std::set<uint64_t> alive{rootId, 1, 2, 3};
        std::map<uint64_t, services::TransformData> transforms;

        int createCount = 0;
        std::string createdName;
        std::optional<services::EntityHandle> createdParent;
        int setTransformCount = 0;
        // VK-1651: mutating tools push one undo entry each.
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;

        void registerHandlers()
        {
            auto& dispatcher = events::EventDispatcher::instance();

            dispatcher.registerQueryHandler<events::scene::GetEntityQuery>(
                [this](const events::scene::GetEntityQuery& query) -> std::optional<services::EntityData>
                {
                    if (!alive.contains(query.entity.id))
                    {
                        return std::nullopt;
                    }
                    return entityData(query.entity.id, "Entity" + std::to_string(query.entity.id), rootId);
                });

            dispatcher.registerQueryHandler<events::scene::GetRootEntityQuery>(
                [](const events::scene::GetRootEntityQuery&)
                {
                    return handleOf(rootId);
                });

            dispatcher.registerCommandHandler<events::scene::CreateEntityCommand>(
                [this](const events::scene::CreateEntityCommand& command)
                {
                    ++createCount;
                    createdName = command.name;
                    createdParent = command.parent;
                    alive.insert(createdId);
                    return handleOf(createdId);
                });

            dispatcher.registerQueryHandler<events::scene::GetTransformQuery>(
                [this](const events::scene::GetTransformQuery& query) -> std::optional<services::TransformData>
                {
                    if (!alive.contains(query.entity.id))
                    {
                        return std::nullopt;
                    }
                    auto it = transforms.find(query.entity.id);
                    return it != transforms.end() ? it->second : services::TransformData{};
                });

            dispatcher.registerCommandHandler<events::scene::SetTransformCommand>(
                [this](const events::scene::SetTransformCommand& command)
                {
                    ++setTransformCount;
                    transforms[command.entity.id] = command.transform;
                });

            dispatcher.registerQueryHandler<events::scene::CopyEntityToJsonQuery>(
                [this](const events::scene::CopyEntityToJsonQuery& query) -> std::string
                {
                    if (!alive.contains(query.entity.id) || query.entity.id == rootId)
                    {
                        return {};
                    }
                    return nlohmann::json{{"name", "Entity" + std::to_string(query.entity.id)},
                                          {"children", nlohmann::json::array()}}.dump();
                });

            dispatcher.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command)
                {
                    undoStack.push_back(command.command);
                });

            dispatcher.registerQueryHandler<events::scene::GetSceneHierarchyQuery>(
                [](const events::scene::GetSceneHierarchyQuery&)
                {
                    services::SceneHierarchyData hierarchy;
                    hierarchy.root = handleOf(rootId);

                    hierarchy.entities.push_back(entityData(rootId, "Root", std::nullopt, {1, 2}));

                    services::EntityData floor = entityData(1, "Floor", rootId);
                    floor.components = {
                        services::ComponentTypeId::Transform,
                        services::ComponentTypeId::Name,
                        services::ComponentTypeId::Mesh,
                        services::ComponentTypeId::Collider
                    };
                    floor.localTransform.position = {0.0f, -1.0f, 0.0f};
                    hierarchy.entities.push_back(floor);

                    services::EntityData player = entityData(2, "Player", rootId, {3});
                    player.isActive = false;
                    hierarchy.entities.push_back(player);

                    services::EntityData camera = entityData(3, "Camera", 2);
                    camera.components = {services::ComponentTypeId::Camera};
                    camera.isEffectivelyActive = false;
                    hierarchy.entities.push_back(camera);
                    return hierarchy;
                });
        }
    };

    struct ToolFixture
    {
        DispatcherScope dispatcherScope;
        FakeScene scene;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        ToolFixture()
        {
            scene.registerHandlers();
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerEntityTools(registry, context);
            mcp::tools::registerSceneTools(registry, context);
        }

        mcp::ToolResult call(const char* name, const nlohmann::json& args)
        {
            auto tool = registry.find(name);
            REQUIRE(tool != nullptr);
            return tool->handler(args);
        }
    };

    void checkVec3(const nlohmann::json& value, float x, float y, float z)
    {
        REQUIRE(value.is_array());
        REQUIRE(value.size() == 3);
        CHECK(value[0].get<float>() == doctest::Approx(x));
        CHECK(value[1].get<float>() == doctest::Approx(y));
        CHECK(value[2].get<float>() == doctest::Approx(z));
    }
}

TEST_SUITE("McpEntityTools")
{
    TEST_CASE("entity_create maps name, parent and transform onto the commands")
    {
        ToolFixture fixture;

        auto result = fixture.call("entity_create", {
            {"name", "Player"},
            {"parent", 1},
            {"position", {1.0, 2.0, 3.0}},
            {"rotationEuler", {0.0, 90.0, 0.0}}
        });

        REQUIRE_FALSE(result.isError);
        CHECK(fixture.scene.createCount == 1);
        CHECK(fixture.scene.createdName == "Player");
        REQUIRE(fixture.scene.createdParent.has_value());
        CHECK(fixture.scene.createdParent->id == 1);

        CHECK(result.structured["id"] == FakeScene::createdId);
        CHECK(result.structured["parent"] == 1);

        CHECK(fixture.scene.setTransformCount == 1);
        const services::TransformData& written = fixture.scene.transforms.at(FakeScene::createdId);
        CHECK(written.position == glm::vec3(1.0f, 2.0f, 3.0f));
        CHECK(written.rotation == glm::vec3(0.0f, 90.0f, 0.0f));
        CHECK(written.scale == glm::vec3(1.0f, 1.0f, 1.0f));
        CHECK(fixture.scene.undoStack.size() == 1);
    }

    TEST_CASE("entity_create without parent or transform targets the root and skips SetTransform")
    {
        ToolFixture fixture;

        auto result = fixture.call("entity_create", nlohmann::json::object());

        REQUIRE_FALSE(result.isError);
        CHECK(fixture.scene.createdName == "Entity");
        CHECK_FALSE(fixture.scene.createdParent.has_value());
        CHECK(fixture.scene.setTransformCount == 0);
        CHECK(result.structured["parent"].is_null());
    }

    TEST_CASE("entity_set_transform merges only the provided fields")
    {
        ToolFixture fixture;
        services::TransformData initial;
        initial.position = {1.0f, 1.0f, 1.0f};
        initial.rotation = {10.0f, 20.0f, 30.0f};
        initial.scale = {2.0f, 2.0f, 2.0f};
        fixture.scene.transforms[1] = initial;

        auto result = fixture.call("entity_set_transform", {
            {"entity", 1},
            {"rotationEuler", {0.0, 90.0, 0.0}}
        });

        REQUIRE_FALSE(result.isError);
        CHECK(fixture.scene.setTransformCount == 1);
        const services::TransformData& written = fixture.scene.transforms.at(1);
        CHECK(written.position == initial.position);
        CHECK(written.rotation == glm::vec3(0.0f, 90.0f, 0.0f));
        CHECK(written.scale == initial.scale);

        checkVec3(result.structured["position"], 1.0f, 1.0f, 1.0f);
        checkVec3(result.structured["rotation"], 0.0f, 90.0f, 0.0f);
        checkVec3(result.structured["scale"], 2.0f, 2.0f, 2.0f);
    }

    TEST_CASE("missing or malformed arguments throw ArgError before any engine call")
    {
        ToolFixture fixture;

        SUBCASE("entity_set_transform without entity")
        {
            CHECK_THROWS_AS((fixture.call("entity_set_transform", {{"position", {0.0, 0.0, 0.0}}})), mcp::ArgError);
        }

        SUBCASE("entity_set_transform without any transform field")
        {
            CHECK_THROWS_AS((fixture.call("entity_set_transform", {{"entity", 1}})), mcp::ArgError);
        }

        SUBCASE("entity_create with a two-component vector creates nothing")
        {
            CHECK_THROWS_AS((fixture.call("entity_create", {{"name", "Bad"}, {"position", {1.0, 2.0}}})), mcp::ArgError);
            CHECK(fixture.scene.createCount == 0);
        }

        SUBCASE("entity_rename without name")
        {
            CHECK_THROWS_AS((fixture.call("entity_rename", {{"entity", 1}})), mcp::ArgError);
        }

        CHECK(fixture.scene.setTransformCount == 0);
    }

    TEST_CASE("unknown entity ids are reported as tool errors, not argument errors")
    {
        ToolFixture fixture;

        CHECK_THROWS_AS((fixture.call("entity_set_transform", {{"entity", 999}, {"position", {0.0, 0.0, 0.0}}})),
                        std::runtime_error);
        CHECK_THROWS_AS((fixture.call("entity_create", {{"parent", 999}})), std::runtime_error);
        CHECK(fixture.scene.createCount == 0);
        CHECK(fixture.scene.setTransformCount == 0);
    }

    TEST_CASE("entity_delete refuses the scene root")
    {
        ToolFixture fixture;
        CHECK_THROWS_AS((fixture.call("entity_delete", {{"entity", FakeScene::rootId}})), std::runtime_error);
    }

    TEST_CASE("scene_get_hierarchy nests children and maps component names")
    {
        ToolFixture fixture;

        auto result = fixture.call("scene_get_hierarchy", nlohmann::json::object());
        REQUIRE_FALSE(result.isError);

        const nlohmann::json& out = result.structured;
        CHECK(out["root"] == FakeScene::rootId);
        CHECK(out["entityCount"] == 3);
        CHECK(out["returned"] == 3);

        const nlohmann::json& entities = out["entities"];
        REQUIRE(entities.size() == 2);

        const nlohmann::json& floor = entities[0];
        CHECK(floor["id"] == 1);
        CHECK(floor["name"] == "Floor");
        CHECK(floor["components"] == nlohmann::json::array({"Mesh", "Collider"}));
        checkVec3(floor["position"], 0.0f, -1.0f, 0.0f);
        CHECK_FALSE(floor.contains("children"));

        const nlohmann::json& player = entities[1];
        CHECK(player["active"] == false);
        REQUIRE(player["children"].size() == 1);
        const nlohmann::json& camera = player["children"][0];
        CHECK(camera["id"] == 3);
        CHECK(camera["components"] == nlohmann::json::array({"Camera"}));
        CHECK(camera["effectivelyActive"] == false);
    }

    TEST_CASE("scene_get_hierarchy honours maxDepth and includeTransforms")
    {
        ToolFixture fixture;

        auto result = fixture.call("scene_get_hierarchy", {{"maxDepth", 0}, {"includeTransforms", false}});
        REQUIRE_FALSE(result.isError);

        const nlohmann::json& player = result.structured["entities"][1];
        CHECK_FALSE(player.contains("children"));
        CHECK(player["childCount"] == 1);
        CHECK_FALSE(player.contains("position"));
        CHECK(result.structured["returned"] == 2);

        CHECK_THROWS_AS((fixture.call("scene_get_hierarchy", {{"maxDepth", -5}})), mcp::ArgError);
    }
}
