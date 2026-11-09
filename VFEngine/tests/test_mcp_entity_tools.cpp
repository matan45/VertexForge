#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/terrain/OceanEvents.hpp"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
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
        // VK-1653: entities GetEntityQuery serves with their real children and components.
        std::map<uint64_t, services::EntityData> detailed;

        int createCount = 0;
        std::string createdName;
        std::optional<services::EntityHandle> createdParent;
        int setTransformCount = 0;
        // VK-1651: mutating tools push one undo entry each.
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;

        // root -> 60 "Level" -> 50 "Terrain" -> 51 "Tile_0_0"
        void addTerrain()
        {
            auto add = [this](uint64_t id, const char* name, uint64_t parent, std::vector<uint64_t> children,
                              std::vector<services::ComponentTypeId> components)
            {
                services::EntityData data = entityData(id, name, parent, std::move(children));
                data.components = std::move(components);
                detailed[id] = std::move(data);
                alive.insert(id);
            };
            add(60, "Level", rootId, {50}, {});
            add(50, "Terrain", 60, {51}, {services::ComponentTypeId::Terrain});
            add(51, "Tile_0_0", 50, {}, {services::ComponentTypeId::TerrainTile});
        }

        // VK-1658: root -> 80 "Coast" -> {70 "Ocean", 72 "Lake"}, root -> 73 "Pool". Ocean and water
        // body membership is answered by registerWaterHandlers() only.
        std::set<uint64_t> oceans;
        std::set<uint64_t> waterBodies;
        int setActiveCount = 0;
        std::vector<std::pair<uint64_t, uint64_t>> reparents;  // (entity, new parent)
        int deleteCount = 0;

        void addWater()
        {
            auto add = [this](uint64_t id, const char* name, uint64_t parent, std::vector<uint64_t> children)
            {
                detailed[id] = entityData(id, name, parent, std::move(children));
                alive.insert(id);
            };
            add(80, "Coast", rootId, {70, 72});
            add(70, "Ocean", 80, {});
            add(72, "Lake", 80, {});
            add(73, "Pool", rootId, {});
            oceans = {70};
            waterBodies = {72, 73};
        }

        // Only the VK-1658 cases register these; every other case runs without an ocean service, which
        // the entity tools must treat as "no water".
        void registerWaterHandlers()
        {
            auto& dispatcher = events::EventDispatcher::instance();

            dispatcher.registerQueryHandler<events::ocean::HasOceanComponentQuery>(
                [this](const events::ocean::HasOceanComponentQuery& query)
                {
                    return oceans.contains(query.entity.id);
                });

            dispatcher.registerQueryHandler<events::ocean::HasWaterBodyComponentQuery>(
                [this](const events::ocean::HasWaterBodyComponentQuery& query)
                {
                    return waterBodies.contains(query.entity.id);
                });
        }

        // The hierarchy edits the success paths of entity_set_active / entity_set_parent / entity_delete need.
        void registerHierarchyEditHandlers()
        {
            auto& dispatcher = events::EventDispatcher::instance();

            dispatcher.registerCommandHandler<events::scene::SetEntityActiveCommand>(
                [this](const events::scene::SetEntityActiveCommand&)
                {
                    ++setActiveCount;
                });

            dispatcher.registerCommandHandler<events::scene::ReparentEntityCommand>(
                [this](const events::scene::ReparentEntityCommand& command)
                {
                    reparents.emplace_back(command.entity.id, command.newParent.id);
                    return true;
                });

            dispatcher.registerCommandHandler<events::scene::DeleteEntityCommand>(
                [this](const events::scene::DeleteEntityCommand& command)
                {
                    ++deleteCount;
                    alive.erase(command.entity.id);
                    return true;
                });
        }

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
                    auto it = detailed.find(query.entity.id);
                    if (it != detailed.end())
                    {
                        return it->second;
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

    bool contains(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
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

    TEST_CASE("terrains and their tiles are refused by the generic entity tools")
    {
        ToolFixture fixture;
        fixture.scene.addTerrain();

        auto error = [&fixture](const char* tool, const nlohmann::json& args)
        {
            try
            {
                fixture.call(tool, args);
            }
            catch (const std::runtime_error& e)
            {
                return std::string(e.what());
            }
            return std::string();
        };

        CHECK(contains(error("entity_delete", {{"entity", 50}}), "terrain_delete"));
        CHECK(contains(error("entity_delete", {{"entity", 51}}), "it is a terrain tile"));
        CHECK(contains(error("entity_delete", {{"entity", 60}}), "contains terrain entity 50"));
        CHECK(contains(error("entity_duplicate", {{"entity", 50}}), "terrain_create"));
        CHECK(contains(error("entity_duplicate", {{"entity", 60}}), "contains terrain entity 50"));
        CHECK(contains(error("entity_set_parent", {{"entity", 50}}), "it is a terrain"));
        CHECK(contains(error("entity_set_parent", {{"entity", 1}, {"parent", 51}}), "it is a terrain tile"));
        CHECK(contains(error("entity_set_parent", {{"entity", 1}, {"parent", 50}}), "it is a terrain"));
        CHECK(contains(error("entity_set_transform", {{"entity", 51}, {"position", {0.0, 0.0, 0.0}}}), "terrain tile"));
        CHECK(contains(error("entity_create", {{"name", "Rock"}, {"parent", 50}}), "it is a terrain"));
        CHECK(fixture.scene.createCount == 0);
        CHECK(fixture.scene.setTransformCount == 0);
        CHECK(fixture.scene.undoStack.empty());

        // rejectRoot still runs first.
        CHECK(contains(error("entity_delete", {{"entity", FakeScene::rootId}}), "scene root"));
    }

    TEST_CASE("the ocean is refused by the generic entity tools (VK-1658)")
    {
        ToolFixture fixture;
        fixture.scene.addWater();
        fixture.scene.registerWaterHandlers();
        fixture.scene.registerHierarchyEditHandlers();

        auto error = [&fixture](const char* tool, const nlohmann::json& args)
        {
            try
            {
                fixture.call(tool, args);
            }
            catch (const std::runtime_error& e)
            {
                return std::string(e.what());
            }
            return std::string();
        };

        const std::string deleted = error("entity_delete", {{"entity", 70}});
        CHECK(contains(deleted, "Cannot delete entity 70 ('Ocean'): it is the ocean."));
        CHECK(contains(deleted, "ocean_enable {enabled:false, remove:true}"));
        CHECK(contains(error("entity_delete", {{"entity", 80}}), "it contains ocean entity 70 ('Ocean')"));
        CHECK(contains(error("entity_duplicate", {{"entity", 70}}), "it is the ocean"));
        CHECK(contains(error("entity_duplicate", {{"entity", 80}}), "it contains ocean entity 70"));
        CHECK(contains(error("entity_set_transform", {{"entity", 70}, {"position", {0.0, 5.0, 0.0}}}),
                       "ocean_set_params {level}"));
        const std::string deactivated = error("entity_set_active", {{"entity", 70}, {"active", false}});
        CHECK(contains(deactivated, "Cannot deactivate entity 70"));
        CHECK(contains(deactivated, "ocean_enable {enabled:false}"));
        const std::string activated = error("entity_set_active", {{"entity", 70}, {"active", true}});
        CHECK(contains(activated, "Cannot activate entity 70"));
        CHECK(contains(activated, "ocean_enable {enabled:true}"));
        CHECK(fixture.scene.deleteCount == 0);
        CHECK(fixture.scene.setActiveCount == 0);
        CHECK(fixture.scene.setTransformCount == 0);
        CHECK(fixture.scene.undoStack.empty());

        // A water body is an ordinary entity for these tools.
        auto moved = fixture.call("entity_set_transform", {{"entity", 73}, {"position", {1.0, 2.0, 3.0}}});
        REQUIRE_FALSE(moved.isError);
        CHECK(fixture.scene.setTransformCount == 1);
        auto removed = fixture.call("entity_delete", {{"entity", 73}});
        REQUIRE_FALSE(removed.isError);
        CHECK(fixture.scene.deleteCount == 1);
        CHECK(fixture.scene.undoStack.size() == 2);
    }

    TEST_CASE("entity_set_active and entity_set_parent warn about water (VK-1658)")
    {
        ToolFixture fixture;
        fixture.scene.addWater();
        fixture.scene.registerWaterHandlers();
        fixture.scene.registerHierarchyEditHandlers();

        SUBCASE("deactivating a water body warns and still applies")
        {
            auto result = fixture.call("entity_set_active", {{"entity", 73}, {"active", false}});
            REQUIRE_FALSE(result.isError);
            CHECK(fixture.scene.setActiveCount == 1);
            CHECK(fixture.scene.undoStack.size() == 1);
            REQUIRE(result.structured.contains("warnings"));
            REQUIRE(result.structured["warnings"].size() == 1);
            CHECK(contains(result.structured["warnings"][0].get<std::string>(), "water_set_body {active:false}"));
        }

        SUBCASE("a group holding the ocean and a lake gets both notes")
        {
            auto result = fixture.call("entity_set_active", {{"entity", 80}, {"active", false}});
            REQUIRE_FALSE(result.isError);
            CHECK(fixture.scene.setActiveCount == 1);
            REQUIRE(result.structured["warnings"].size() == 2);
            CHECK(contains(result.structured["warnings"][0].get<std::string>(), "ocean_enable"));
            CHECK(contains(result.structured["warnings"][1].get<std::string>(), "water_set_body"));
        }

        SUBCASE("an entity without water gets no warnings")
        {
            auto result = fixture.call("entity_set_active", {{"entity", 1}, {"active", false}});
            REQUIRE_FALSE(result.isError);
            CHECK_FALSE(result.structured.contains("warnings"));
        }

        SUBCASE("moving a water body under another entity warns; the scene root does not")
        {
            auto nested = fixture.call("entity_set_parent", {{"entity", 73}, {"parent", 1}});
            REQUIRE_FALSE(nested.isError);
            REQUIRE(fixture.scene.reparents.size() == 1);
            CHECK(fixture.scene.reparents[0].first == 73);
            CHECK(fixture.scene.reparents[0].second == 1);
            REQUIRE(nested.structured.contains("warnings"));
            CHECK(contains(nested.structured["warnings"][0].get<std::string>(), "LOCAL position"));

            auto toRoot = fixture.call("entity_set_parent", {{"entity", 73}});
            REQUIRE_FALSE(toRoot.isError);
            CHECK_FALSE(toRoot.structured.contains("warnings"));

            // A group with a lake inside warns too; one without water does not.
            auto group = fixture.call("entity_set_parent", {{"entity", 80}, {"parent", 1}});
            REQUIRE_FALSE(group.isError);
            CHECK(group.structured.contains("warnings"));

            auto plain = fixture.call("entity_set_parent", {{"entity", 1}, {"parent", 2}});
            REQUIRE_FALSE(plain.isError);
            CHECK_FALSE(plain.structured.contains("warnings"));
        }
    }

    TEST_CASE("a subtree holding both a terrain and the ocean is refused for the terrain first (VK-1658)")
    {
        ToolFixture fixture;
        fixture.scene.addTerrain();
        fixture.scene.addWater();
        // root -> 90 "World" -> {60 "Level" -> 50 "Terrain", 80 "Coast" -> 70 "Ocean"}
        fixture.scene.detailed[90] = entityData(90, "World", FakeScene::rootId, {60, 80});
        fixture.scene.alive.insert(90);
        fixture.scene.registerWaterHandlers();
        fixture.scene.registerHierarchyEditHandlers();

        for (const char* tool : {"entity_delete", "entity_duplicate"})
        {
            CAPTURE(tool);
            std::string message;
            try
            {
                fixture.call(tool, {{"entity", 90}});
            }
            catch (const std::runtime_error& e)
            {
                message = e.what();
            }
            CHECK(contains(message, "it contains terrain entity 50"));
        }
        CHECK(fixture.scene.deleteCount == 0);
        CHECK(fixture.scene.undoStack.empty());
    }

    TEST_CASE("without an ocean service the entity tools behave as before (VK-1658)")
    {
        // No HasOceanComponent / HasWaterBodyComponent handler: the water checks read "no water".
        ToolFixture fixture;
        fixture.scene.addWater();
        fixture.scene.registerHierarchyEditHandlers();

        auto active = fixture.call("entity_set_active", {{"entity", 70}, {"active", false}});
        REQUIRE_FALSE(active.isError);
        CHECK(fixture.scene.setActiveCount == 1);
        CHECK_FALSE(active.structured.contains("warnings"));

        auto moved = fixture.call("entity_set_parent", {{"entity", 73}, {"parent", 1}});
        REQUIRE_FALSE(moved.isError);
        CHECK_FALSE(moved.structured.contains("warnings"));

        auto transformed = fixture.call("entity_set_transform", {{"entity", 70}, {"position", {0.0, 1.0, 0.0}}});
        REQUIRE_FALSE(transformed.isError);
        CHECK(fixture.scene.setTransformCount == 1);

        auto deleted = fixture.call("entity_delete", {{"entity", 80}});
        REQUIRE_FALSE(deleted.isError);
        CHECK(fixture.scene.deleteCount == 1);
        CHECK(fixture.scene.undoStack.size() == 4);
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
