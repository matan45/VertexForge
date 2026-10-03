#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "undo/EntityIdRemap.hpp"
#include "undo/McpUndoCommands.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// VK-1651: MCP agent edits push one undo entry per tool call; undo/redo replay the
// same CQRS commands and follow re-minted entity ids through EntityIdRemap.
namespace
{
    struct DispatcherScope
    {
        DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
        }

        ~DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
        }
    };

    services::EntityHandle handleOf(uint64_t id)
    {
        services::EntityHandle handle;
        handle.id = id;
        return handle;
    }

    // Same semantics as SceneGraphSystem::moveEntity: insertIndex refers to the child
    // list BEFORE the move; a same-parent move from in front of the slot decrements it.
    void moveInList(std::vector<uint64_t>& oldList, std::vector<uint64_t>& newList, bool sameParent, uint64_t id,
                    int insertIndex)
    {
        if (insertIndex >= 0 && sameParent)
        {
            auto it = std::find(oldList.begin(), oldList.end(), id);
            if (it != oldList.end() && std::distance(oldList.begin(), it) < insertIndex)
            {
                --insertIndex;
            }
        }
        oldList.erase(std::remove(oldList.begin(), oldList.end(), id), oldList.end());
        if (insertIndex < 0)
        {
            newList.push_back(id);
            return;
        }
        const std::size_t index = std::min(static_cast<std::size_t>(insertIndex), newList.size());
        newList.insert(newList.begin() + static_cast<std::ptrdiff_t>(index), id);
    }

    // A small but faithful scene: ids are never reused, delete removes the subtree,
    // instantiate mints fresh ids, reorder follows the engine's index rule.
    //   root 100 -> {1 "Floor", 2 "Player" -> {4 "Gun"}, 3 "Light"}
    struct FakeWorld
    {
        static constexpr uint64_t rootId = 100;

        struct Node
        {
            std::string name;
            bool active = true;
            std::optional<uint64_t> parent;
            std::vector<uint64_t> children;
            services::TransformData transform;
        };

        std::map<uint64_t, Node> nodes;
        uint64_t nextId = 200;
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;
        std::vector<std::shared_ptr<services::IUndoableCommand>> redoStack;
        std::map<uint64_t, services::PointLightData> pointLights;
        int setTransformCount = 0;

        FakeWorld()
        {
            nodes[rootId] = Node{"Root", true, std::nullopt, {1, 2, 3}, {}};
            nodes[1] = Node{"Floor", true, rootId, {}, {}};
            nodes[2] = Node{"Player", true, rootId, {4}, {}};
            nodes[3] = Node{"Light", true, rootId, {}, {}};
            nodes[4] = Node{"Gun", true, 2, {}, {}};
        }

        bool alive(uint64_t id) const { return nodes.contains(id); }

        std::vector<uint64_t>& childrenOf(uint64_t id) { return nodes.at(id).children; }

        uint64_t create(const std::string& name, uint64_t parent)
        {
            const uint64_t id = nextId++;
            nodes[id] = Node{name, true, parent, {}, {}};
            nodes.at(parent).children.push_back(id);
            return id;
        }

        void erase(uint64_t id)
        {
            for (uint64_t child : std::vector<uint64_t>(nodes.at(id).children))
            {
                erase(child);
            }
            nodes.erase(id);
        }

        nlohmann::json serialize(uint64_t id) const
        {
            const Node& node = nodes.at(id);
            nlohmann::json children = nlohmann::json::array();
            for (uint64_t child : node.children)
            {
                children.push_back(serialize(child));
            }
            return {
                {"name", node.name},
                {"isActive", node.active},
                {"transform", {{"position", mcp::vec3ToJson(node.transform.position)}}},
                {"children", std::move(children)}
            };
        }

        uint64_t instantiate(const nlohmann::json& json, uint64_t parent)
        {
            const uint64_t id = create(json.value("name", std::string("Entity")), parent);
            nodes.at(id).active = json.value("isActive", true);
            nodes.at(id).transform.position = mcp::ArgReader::toVec3(json["transform"]["position"], "position");
            for (const nlohmann::json& child : json["children"])
            {
                instantiate(child, id);
            }
            return id;
        }

        int indexIn(uint64_t parent, uint64_t child)
        {
            auto& list = childrenOf(parent);
            auto it = std::find(list.begin(), list.end(), child);
            return it == list.end() ? -1 : static_cast<int>(std::distance(list.begin(), it));
        }

        bool isDescendant(uint64_t ancestor, uint64_t id) const
        {
            for (uint64_t child : nodes.at(ancestor).children)
            {
                if (child == id || isDescendant(child, id))
                {
                    return true;
                }
            }
            return false;
        }

        bool moveEntity(uint64_t id, uint64_t parent, int insertIndex)
        {
            if (!alive(id) || !alive(parent) || id == parent || isDescendant(id, parent))
            {
                return false;
            }
            const uint64_t oldParent = *nodes.at(id).parent;
            moveInList(childrenOf(oldParent), childrenOf(parent), oldParent == parent, id, insertIndex);
            nodes.at(id).parent = parent;
            return true;
        }

        void registerHandlers()
        {
            auto& d = events::EventDispatcher::instance();
            using namespace events::scene;

            d.registerQueryHandler<GetEntityQuery>(
                [this](const GetEntityQuery& q) -> std::optional<services::EntityData>
                {
                    if (!alive(q.entity.id))
                    {
                        return std::nullopt;
                    }
                    const Node& node = nodes.at(q.entity.id);
                    services::EntityData data;
                    data.handle = q.entity;
                    data.name = node.name;
                    data.isActive = node.active;
                    if (node.parent.has_value())
                    {
                        data.parent = handleOf(*node.parent);
                    }
                    for (uint64_t child : node.children)
                    {
                        data.children.push_back(handleOf(child));
                    }
                    data.localTransform = node.transform;
                    return data;
                });
            d.registerQueryHandler<GetRootEntityQuery>([](const GetRootEntityQuery&) { return handleOf(rootId); });
            d.registerQueryHandler<GetTransformQuery>(
                [this](const GetTransformQuery& q) -> std::optional<services::TransformData>
                {
                    if (!alive(q.entity.id))
                    {
                        return std::nullopt;
                    }
                    return nodes.at(q.entity.id).transform;
                });
            d.registerCommandHandler<SetTransformCommand>(
                [this](const SetTransformCommand& c)
                {
                    ++setTransformCount;
                    if (alive(c.entity.id))
                    {
                        nodes.at(c.entity.id).transform = c.transform;
                    }
                });
            d.registerCommandHandler<CreateEntityCommand>(
                [this](const CreateEntityCommand& c)
                {
                    return handleOf(create(c.name, c.parent.has_value() ? c.parent->id : rootId));
                });
            d.registerCommandHandler<DeleteEntityCommand>(
                [this](const DeleteEntityCommand& c)
                {
                    if (!alive(c.entity.id) || c.entity.id == rootId)
                    {
                        return false;
                    }
                    auto& siblings = childrenOf(*nodes.at(c.entity.id).parent);
                    siblings.erase(std::remove(siblings.begin(), siblings.end(), c.entity.id), siblings.end());
                    erase(c.entity.id);
                    return true;
                });
            d.registerQueryHandler<CopyEntityToJsonQuery>(
                [this](const CopyEntityToJsonQuery& q) -> std::string
                {
                    if (!alive(q.entity.id) || q.entity.id == rootId)
                    {
                        return {};
                    }
                    return serialize(q.entity.id).dump();
                });
            d.registerCommandHandler<InstantiateEntityFromJsonCommand>(
                [this](const InstantiateEntityFromJsonCommand& c) -> std::optional<services::EntityHandle>
                {
                    const uint64_t parent = c.parent.has_value() && alive(c.parent->id) ? c.parent->id : rootId;
                    return handleOf(instantiate(nlohmann::json::parse(c.jsonText), parent));
                });
            d.registerCommandHandler<ReparentEntityCommand>(
                [this](const ReparentEntityCommand& c) { return moveEntity(c.entity.id, c.newParent.id, -1); });
            d.registerCommandHandler<ReorderEntityCommand>(
                [this](const ReorderEntityCommand& c) { return moveEntity(c.entity.id, c.newParent.id, c.insertIndex); });
            d.registerCommandHandler<SetEntityNameCommand>(
                [this](const SetEntityNameCommand& c)
                {
                    if (alive(c.entity.id))
                    {
                        nodes.at(c.entity.id).name = c.newName;
                    }
                });
            d.registerCommandHandler<SetEntityActiveCommand>(
                [this](const SetEntityActiveCommand& c)
                {
                    if (alive(c.entity.id))
                    {
                        nodes.at(c.entity.id).active = c.isActive;
                    }
                });

            d.registerQueryHandler<GetPointLightDataQuery>(
                [this](const GetPointLightDataQuery& q) -> std::optional<services::PointLightData>
                {
                    auto it = pointLights.find(q.entity.id);
                    return it != pointLights.end() ? std::optional(it->second) : std::nullopt;
                });
            d.registerCommandHandler<SetPointLightDataCommand>(
                [this](const SetPointLightDataCommand& c)
                {
                    if (!pointLights.contains(c.entity.id) || c.lightData.intensity < 0.0f)
                    {
                        return false;
                    }
                    pointLights[c.entity.id] = c.lightData;
                    return true;
                });
            d.registerCommandHandler<AddPointLightComponentCommand>(
                [this](const AddPointLightComponentCommand& c)
                {
                    return pointLights.emplace(c.entity.id, services::PointLightData{}).second;
                });
            d.registerCommandHandler<RemovePointLightComponentCommand>(
                [this](const RemovePointLightComponentCommand& c) { return pointLights.erase(c.entity.id) > 0; });

            // Fake undo service: same stack discipline as UndoRedoServiceImpl.
            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& c)
                {
                    undoStack.push_back(c.command);
                    redoStack.clear();
                });
            d.registerCommandHandler<events::undoredo::UndoCommand>(
                [this](const events::undoredo::UndoCommand&)
                {
                    if (undoStack.empty())
                    {
                        return false;
                    }
                    auto command = undoStack.back();
                    undoStack.pop_back();
                    command->undo();
                    redoStack.push_back(command);
                    return true;
                });
            d.registerCommandHandler<events::undoredo::RedoCommand>(
                [this](const events::undoredo::RedoCommand&)
                {
                    if (redoStack.empty())
                    {
                        return false;
                    }
                    auto command = redoStack.back();
                    redoStack.pop_back();
                    command->execute();
                    undoStack.push_back(command);
                    return true;
                });
            d.registerQueryHandler<events::undoredo::GetUndoHistoryStatsQuery>(
                [this](const events::undoredo::GetUndoHistoryStatsQuery&)
                {
                    services::UndoHistoryStats stats;
                    stats.undoCount = undoStack.size();
                    stats.redoCount = redoStack.size();
                    return stats;
                });
        }
    };

    struct UndoFixture
    {
        DispatcherScope dispatcherScope;
        FakeWorld world;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        UndoFixture()
        {
            world.registerHandlers();
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerEntityTools(registry, context);
            mcp::tools::registerComponentTools(registry, context);
            mcp::tools::registerUndoTools(registry, context);
        }

        mcp::ToolResult call(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolDef* tool = registry.find(name);
            REQUIRE(tool != nullptr);
            return tool->handler(args);
        }

        uint64_t resolve(uint64_t recordedId) const
        {
            return mcp::undo::EntityIdRemap::instance().resolve(static_cast<uint32_t>(recordedId));
        }
    };
}

TEST_SUITE("McpUndo")
{
    TEST_CASE("reorderInsertIndex lands the entity on the desired slot for every move")
    {
        CHECK(mcp::undo::reorderInsertIndex(std::nullopt, 2) == 2);
        CHECK(mcp::undo::reorderInsertIndex(4, 1) == 1);   // moving forward: no correction
        CHECK(mcp::undo::reorderInsertIndex(1, 1) == 1);   // already there
        CHECK(mcp::undo::reorderInsertIndex(1, 3) == 4);   // in front of the slot: ask for one more

        // Exhaustive same-parent check against the engine's rule: from final position k
        // back to original position j, for every (j, k) in a 5-child list.
        for (int j = 0; j < 5; ++j)
        {
            for (int k = 0; k < 5; ++k)
            {
                std::vector<uint64_t> list{10, 11, 12, 13, 14};
                const uint64_t id = list[static_cast<std::size_t>(j)];
                list.erase(list.begin() + j);
                list.insert(list.begin() + k, id);

                moveInList(list, list, true, id, mcp::undo::reorderInsertIndex(k, j));
                CAPTURE(j);
                CAPTURE(k);
                CHECK(list[static_cast<std::size_t>(j)] == id);
                CHECK(list.size() == 5);
            }
        }

        // From another parent there is no correction: the old index is used as-is.
        std::vector<uint64_t> oldParent{20};
        std::vector<uint64_t> target{10, 11, 12};
        moveInList(oldParent, target, false, 20, mcp::undo::reorderInsertIndex(std::nullopt, 1));
        CHECK(target == std::vector<uint64_t>{10, 20, 11, 12});
    }

    TEST_CASE("alignChildren zips equal lists and skips live-only children otherwise")
    {
        using mcp::undo::alignChildren;
        CHECK(alignChildren({"A", "B"}, {"X", "Y"}) == std::vector<int>{0, 1});
        CHECK(alignChildren({"A", "B"}, {"A", "item", "item", "B"}) == std::vector<int>{0, 3});
        CHECK(alignChildren({"A", "A"}, {"A", "item", "A"}) == std::vector<int>{0, 2});
        CHECK(alignChildren({"A", "C"}, {"A", "B", "B"}) == std::vector<int>{0, -1});
        CHECK(alignChildren({}, {"item"}).empty());
    }

    TEST_CASE("EntityIdRemap resolves chains transitively and reports changes")
    {
        DispatcherScope scope;
        auto& remap = mcp::undo::EntityIdRemap::instance();

        CHECK(remap.resolve(5) == 5);
        remap.record(5, 6);
        const auto before = remap.snapshot();
        remap.record(6, 7);
        CHECK(remap.resolve(5) == 7);
        CHECK(remap.resolve(6) == 7);
        remap.record(5, 8);  // via the original id: the whole chain follows
        CHECK(remap.resolve(5) == 8);
        CHECK(remap.resolve(6) == 8);
        CHECK(remap.resolve(7) == 8);
        CHECK(remap.resolve(8) == 8);

        auto changed = mcp::undo::EntityIdRemap::changes(before, remap.snapshot());
        CHECK(std::find(changed.begin(), changed.end(), std::pair<uint32_t, uint32_t>{5, 8}) != changed.end());
        CHECK(std::find(changed.begin(), changed.end(), std::pair<uint32_t, uint32_t>{7, 8}) != changed.end());
    }

    TEST_CASE("the remap is cleared on SceneClearedNotification")
    {
        UndoFixture fixture;
        mcp::undo::EntityIdRemap::instance().record(1, 2);
        REQUIRE(fixture.resolve(1) == 2);

        events::EventDispatcher::instance().publish(events::scene::SceneClearedNotification{});
        CHECK(fixture.resolve(1) == 1);
    }

    TEST_CASE("entity_set_transform pushes one entry that replays before / after")
    {
        UndoFixture fixture;
        fixture.world.nodes.at(1).transform.position = {1.0f, 1.0f, 1.0f};

        auto result = fixture.call("entity_set_transform", {{"entity", 1}, {"position", {5.0, 0.0, 0.0}}});
        REQUIRE_FALSE(result.isError);
        REQUIRE(fixture.world.undoStack.size() == 1);
        auto entry = fixture.world.undoStack.back();
        CHECK(entry->getDescription() == "MCP: Set transform (Floor)");
        CHECK(entry->getMemoryFootprint() > 0);
        CHECK(fixture.world.setTransformCount == 1);  // push did not execute the entry

        entry->undo();
        CHECK(fixture.world.nodes.at(1).transform.position == glm::vec3(1.0f, 1.0f, 1.0f));
        entry->execute();
        CHECK(fixture.world.nodes.at(1).transform.position == glm::vec3(5.0f, 0.0f, 0.0f));
    }

    TEST_CASE("entity_create: undo deletes, redo re-creates with a new id that later entries follow")
    {
        UndoFixture fixture;

        auto created = fixture.call("entity_create", {{"name", "Crate"}, {"position", {1.0, 2.0, 3.0}}});
        REQUIRE_FALSE(created.isError);
        const uint64_t crate = created.structured["id"].get<uint64_t>();
        CHECK(fixture.world.undoStack.size() == 1);

        REQUIRE_FALSE(fixture.call("entity_set_transform", {{"entity", crate}, {"position", {9.0, 9.0, 9.0}}}).isError);
        REQUIRE(fixture.world.undoStack.size() == 2);

        auto undoTransform = fixture.call("undo", nlohmann::json::object());
        CHECK(undoTransform.structured["performed"] == true);
        CHECK(fixture.world.nodes.at(crate).transform.position == glm::vec3(1.0f, 2.0f, 3.0f));

        fixture.call("undo", nlohmann::json::object());
        CHECK_FALSE(fixture.world.alive(crate));

        auto redoCreate = fixture.call("redo", nlohmann::json::object());
        const uint64_t recreated = fixture.resolve(crate);
        CHECK(recreated != crate);
        REQUIRE(fixture.world.alive(recreated));
        CHECK(fixture.world.nodes.at(recreated).name == "Crate");
        CHECK(fixture.world.nodes.at(recreated).transform.position == glm::vec3(1.0f, 2.0f, 3.0f));
        const nlohmann::json& remapped = redoCreate.structured["remappedEntities"];
        REQUIRE(remapped.size() == 1);
        CHECK(remapped[0]["from"] == crate);
        CHECK(remapped[0]["to"] == recreated);

        // The transform entry was recorded against the old id and must hit the new one.
        fixture.call("redo", nlohmann::json::object());
        CHECK(fixture.world.nodes.at(recreated).transform.position == glm::vec3(9.0f, 9.0f, 9.0f));

        // A second round trip chains: the original id still resolves to the live entity.
        fixture.call("undo", nlohmann::json::object());
        fixture.call("undo", nlohmann::json::object());
        fixture.call("redo", nlohmann::json::object());
        CHECK(fixture.world.alive(fixture.resolve(crate)));
        CHECK(fixture.resolve(recreated) == fixture.resolve(crate));
    }

    TEST_CASE("entity_delete: undo restores the subtree at its sibling index and remaps every id")
    {
        UndoFixture fixture;

        REQUIRE_FALSE(fixture.call("entity_delete", {{"entity", 2}}).isError);
        CHECK_FALSE(fixture.world.alive(2));
        CHECK_FALSE(fixture.world.alive(4));
        REQUIRE(fixture.world.undoStack.size() == 1);

        fixture.world.undoStack.back()->undo();
        const uint64_t player = fixture.resolve(2);
        const uint64_t gun = fixture.resolve(4);
        CHECK(player != 2);
        CHECK(gun != 4);
        REQUIRE(fixture.world.alive(player));
        CHECK(fixture.world.nodes.at(player).name == "Player");
        CHECK(fixture.world.childrenOf(FakeWorld::rootId) == std::vector<uint64_t>{1, player, 3});
        CHECK(fixture.world.childrenOf(player) == std::vector<uint64_t>{gun});
        CHECK(fixture.world.nodes.at(gun).name == "Gun");

        fixture.world.undoStack.back()->execute();  // redo deletes the re-created subtree
        CHECK_FALSE(fixture.world.alive(player));
        CHECK_FALSE(fixture.world.alive(gun));
    }

    TEST_CASE("entity_set_parent undo restores the old parent and sibling slot")
    {
        UndoFixture fixture;

        SUBCASE("to another parent")
        {
            REQUIRE_FALSE(fixture.call("entity_set_parent", {{"entity", 2}, {"parent", 3}}).isError);
            CHECK(fixture.world.childrenOf(FakeWorld::rootId) == std::vector<uint64_t>{1, 3});
            fixture.world.undoStack.back()->undo();
            CHECK(fixture.world.childrenOf(FakeWorld::rootId) == std::vector<uint64_t>{1, 2, 3});
            CHECK(fixture.world.childrenOf(3).empty());
            fixture.world.undoStack.back()->execute();
            CHECK(fixture.world.childrenOf(3) == std::vector<uint64_t>{2});
        }

        SUBCASE("to the same parent (moves to the end)")
        {
            REQUIRE_FALSE(fixture.call("entity_set_parent", {{"entity", 1}}).isError);
            CHECK(fixture.world.childrenOf(FakeWorld::rootId) == std::vector<uint64_t>{2, 3, 1});
            fixture.world.undoStack.back()->undo();
            CHECK(fixture.world.childrenOf(FakeWorld::rootId) == std::vector<uint64_t>{1, 2, 3});
        }
    }

    TEST_CASE("rename and set_active round-trip")
    {
        UndoFixture fixture;

        fixture.call("entity_rename", {{"entity", 1}, {"name", "Ground"}});
        fixture.call("entity_set_active", {{"entity", 1}, {"active", false}});
        REQUIRE(fixture.world.undoStack.size() == 2);

        fixture.call("undo", nlohmann::json::object());
        CHECK(fixture.world.nodes.at(1).active);
        fixture.call("undo", nlohmann::json::object());
        CHECK(fixture.world.nodes.at(1).name == "Floor");
        fixture.call("redo", nlohmann::json::object());
        CHECK(fixture.world.nodes.at(1).name == "Ground");
    }

    TEST_CASE("component_set / _add / _remove on a built-in type are undoable")
    {
        UndoFixture fixture;

        REQUIRE_FALSE(fixture.call("component_add", {{"entity", 3}, {"type", "PointLight"},
                                                     {"data", {{"intensity", 4.0}}}}).isError);
        REQUIRE_FALSE(fixture.call("component_set", {{"entity", 3}, {"type", "PointLight"},
                                                     {"data", {{"radius", 2.5}}}}).isError);
        REQUIRE(fixture.world.undoStack.size() == 2);
        CHECK(fixture.world.pointLights.at(3).radius == doctest::Approx(2.5f));

        fixture.call("undo", nlohmann::json::object());
        CHECK(fixture.world.pointLights.at(3).radius == doctest::Approx(10.0f));
        CHECK(fixture.world.pointLights.at(3).intensity == doctest::Approx(4.0f));

        fixture.call("undo", nlohmann::json::object());
        CHECK_FALSE(fixture.world.pointLights.contains(3));
        fixture.call("redo", nlohmann::json::object());
        CHECK(fixture.world.pointLights.at(3).intensity == doctest::Approx(4.0f));

        REQUIRE_FALSE(fixture.call("component_remove", {{"entity", 3}, {"type", "PointLight"}}).isError);
        CHECK_FALSE(fixture.world.pointLights.contains(3));
        fixture.call("undo", nlohmann::json::object());
        CHECK(fixture.world.pointLights.at(3).intensity == doctest::Approx(4.0f));
    }

    TEST_CASE("a failed tool pushes nothing")
    {
        UndoFixture fixture;

        // Into its own descendant: the reparent is refused.
        auto result = fixture.call("entity_set_parent", {{"entity", 2}, {"parent", 4}});
        CHECK(result.isError);
        // Engine rejects the value (negative intensity) after validation.
        fixture.world.pointLights[3] = services::PointLightData{};
        CHECK_THROWS_AS((fixture.call("component_set", {{"entity", 3}, {"type", "PointLight"},
                                                        {"data", {{"intensity", -1.0}}}})), std::runtime_error);
        CHECK_THROWS_AS((fixture.call("entity_set_transform", {{"entity", 999}, {"position", {0.0, 0.0, 0.0}}})),
                        std::runtime_error);
        CHECK(fixture.world.undoStack.empty());
    }
}
