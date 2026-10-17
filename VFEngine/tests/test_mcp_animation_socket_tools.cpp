#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "undo/EntityIdRemap.hpp"

#include "asset/AssetGUID.hpp"
#include "asset/AssetRef.hpp"
#include "events/EventDispatcher.hpp"
#include "events/animation/AnimationAuthoringEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/physics/SocketEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// VK-1656: the MCP socket tools against a fake scene: entities with a hierarchy, transforms, the socket
// names each parent's mesh exposes, the SocketAttachment components (an empty component has no parent and
// no socket; a parent that does not exist is reported unresolved, like SocketAdapter) and the skeletons
// GetMeshSkeletonQuery would read. The log records every mutating command in dispatch order.
namespace
{
    constexpr uint64_t rootId = 100;
    constexpr uint64_t heroId = 10;   // skinned mesh with sockets hand_r, head, ghost
    constexpr uint64_t swordId = 11;  // the usual child
    constexpr uint64_t shieldId = 12; // below the hero in the hierarchy, no sockets
    constexpr uint64_t crateId = 13;  // static mesh with socket top
    constexpr uint64_t gemId = 16;    // below the sword in the hierarchy
    constexpr uint64_t terrainId = 50;
    const std::string heroMesh = "C:/VFTestProject/characters/Hero.vfMesh";
    const std::string crateMesh = "C:/VFTestProject/props/Crate.vfMesh";

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

    asset::AssetRef assetAt(uint64_t guid, const std::string& path)
    {
        return asset::AssetRef::fromGUIDAndPath(asset::AssetGUID::fromValue(guid), path);
    }

    services::TransformData transformAt(glm::vec3 position, glm::vec3 rotation = glm::vec3(0.0f))
    {
        services::TransformData transform;
        transform.position = position;
        transform.rotation = rotation;
        return transform;
    }

    animator::SocketDefinition socketDef(std::string name, std::string bone, int32_t boneIndex, glm::vec3 position,
                                         glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
    {
        animator::SocketDefinition socket;
        socket.name = std::move(name);
        socket.targetBoneName = std::move(bone);
        socket.boneIndex = boneIndex;
        socket.localPosition = position;
        socket.localRotation = rotation;
        return socket;
    }

    bool contains(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }

    bool anyContains(const nlohmann::json& warnings, const std::string& needle)
    {
        for (const nlohmann::json& warning : warnings)
        {
            if (warning.is_string() && contains(warning.get<std::string>(), needle))
            {
                return true;
            }
        }
        return false;
    }

    std::string logText(const std::vector<std::string>& log)
    {
        std::string text;
        for (const std::string& entry : log)
        {
            text += text.empty() ? entry : ", " + entry;
        }
        return "[" + text + "]";
    }

    void checkLog(const std::vector<std::string>& actual, const std::vector<std::string>& expected)
    {
        INFO("expected " << logText(expected));
        INFO("actual   " << logText(actual));
        CHECK(actual == expected);
    }

    struct FakeSocketScene
    {
        struct Link
        {
            std::optional<uint64_t> parent; // nullopt: an empty component
            std::string socket;
            bool active = true;
        };

        std::map<uint64_t, services::EntityData> entities;
        std::map<uint64_t, services::TransformData> transforms;
        std::map<uint64_t, services::TransformData> worlds; // world transforms that differ from the local one
        std::map<uint64_t, std::vector<std::string>> sockets;
        std::map<std::pair<uint64_t, std::string>, glm::vec3> socketPositions; // where AttachToSocket puts the child
        std::map<std::pair<uint64_t, std::string>, glm::mat4> socketWorlds;
        std::map<uint64_t, Link> links;
        std::map<uint64_t, services::MeshData> meshes;
        std::map<std::string, services::MeshSkeletonInfo> skeletons;
        std::vector<std::string> log;
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;

        FakeSocketScene()
        {
            addEntity(rootId, "Root", std::nullopt);
            addEntity(heroId, "Hero", rootId);
            addEntity(swordId, "Sword", rootId);
            addEntity(shieldId, "Shield", heroId);
            addEntity(crateId, "Crate", rootId);
            addEntity(gemId, "Gem", swordId);
            addEntity(terrainId, "Terrain", rootId);
            entities.at(terrainId).components = {services::ComponentTypeId::Terrain};

            sockets[heroId] = {"hand_r", "head", "ghost"};
            sockets[crateId] = {"top"};
            socketPositions[{heroId, "hand_r"}] = glm::vec3(1.0f, 2.0f, 3.0f);
            socketWorlds[{heroId, "hand_r"}] =
                glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)) *
                glm::mat4_cast(glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)));

            services::MeshData hero;
            hero.meshRef = assetAt(1, heroMesh);
            hero.animatorRef = assetAt(2, "C:/VFTestProject/characters/Hero.vfAnimator");
            meshes[heroId] = hero;
            services::MeshData crate;
            crate.meshRef = assetAt(3, crateMesh);
            meshes[crateId] = crate;

            services::MeshSkeletonInfo skinned;
            skinned.success = true;
            skinned.skinned = true;
            skinned.boneNames = {"Hips", "Head", "Hand_R"};
            skinned.parents = {-1, 0, 0};
            skinned.sockets = {
                socketDef("hand_r", "Hand_R", 2, glm::vec3(0.1f, 0.0f, 0.0f),
                          glm::angleAxis(glm::radians(45.0f), glm::vec3(1.0f, 0.0f, 0.0f))),
                socketDef("head", "Head", 1, glm::vec3(0.0f, 0.2f, 0.0f)),
                socketDef("ghost", "Tail", -1, glm::vec3(0.0f))
            };
            skeletons[heroMesh] = skinned;

            services::MeshSkeletonInfo staticMesh;
            staticMesh.success = true;
            staticMesh.sockets = {socketDef("top", "", -1, glm::vec3(0.0f, 1.0f, 0.0f))};
            skeletons[crateMesh] = staticMesh;
        }

        void addEntity(uint64_t id, const std::string& name, std::optional<uint64_t> parent)
        {
            services::EntityData data;
            data.handle = handleOf(id);
            data.name = name;
            if (parent.has_value())
            {
                data.parent = handleOf(*parent);
                entities.at(*parent).children.push_back(handleOf(id));
            }
            entities[id] = data;
        }

        bool alive(uint64_t id) const
        {
            return entities.contains(id);
        }

        // An undone delete re-created the entity under a new id (EntityIdRemap follows it).
        void moveEntity(uint64_t from, uint64_t to)
        {
            entities[to] = entities.at(from);
            entities[to].handle = handleOf(to);
            entities.erase(from);
            if (auto it = sockets.find(from); it != sockets.end())
            {
                sockets[to] = it->second;
                sockets.erase(it);
            }
        }

        void registerHandlers()
        {
            auto& d = events::EventDispatcher::instance();
            using namespace events::scene;
            using namespace events::socket;

            d.registerQueryHandler<GetEntityQuery>(
                [this](const GetEntityQuery& query) -> std::optional<services::EntityData>
                {
                    const auto it = entities.find(query.entity.id);
                    return it != entities.end() ? std::optional<services::EntityData>(it->second) : std::nullopt;
                });
            d.registerQueryHandler<FindEntitiesByNameQuery>(
                [this](const FindEntitiesByNameQuery& query)
                {
                    std::vector<services::EntityHandle> found;
                    for (const auto& [id, data] : entities)
                    {
                        if (data.name == query.name)
                        {
                            found.push_back(handleOf(id));
                        }
                    }
                    return found;
                });
            d.registerQueryHandler<GetEntitiesWithComponentQuery>(
                [this](const GetEntitiesWithComponentQuery& query)
                {
                    std::vector<services::EntityHandle> found;
                    if (query.componentType == services::ComponentTypeId::SocketAttachment)
                    {
                        for (const auto& entry : links)
                        {
                            found.push_back(handleOf(entry.first));
                        }
                    }
                    return found;
                });

            d.registerQueryHandler<GetTransformQuery>(
                [this](const GetTransformQuery& query) -> std::optional<services::TransformData>
                {
                    if (!alive(query.entity.id))
                    {
                        return std::nullopt;
                    }
                    const auto it = transforms.find(query.entity.id);
                    return it != transforms.end() ? it->second : services::TransformData{};
                });
            d.registerCommandHandler<SetTransformCommand>(
                [this](const SetTransformCommand& command)
                {
                    log.push_back("SetTransform:" + std::to_string(command.entity.id));
                    transforms[command.entity.id] = command.transform;
                });
            d.registerQueryHandler<GetWorldTransformQuery>(
                [this](const GetWorldTransformQuery& query) -> std::optional<services::TransformData>
                {
                    if (!alive(query.entity.id))
                    {
                        return std::nullopt;
                    }
                    if (const auto it = worlds.find(query.entity.id); it != worlds.end())
                    {
                        return it->second;
                    }
                    const auto it = transforms.find(query.entity.id);
                    return it != transforms.end() ? it->second : services::TransformData{};
                });
            // Every hierarchy parent sits at the origin, so the world transform becomes the local one.
            d.registerCommandHandler<SetWorldTransformCommand>(
                [this](const SetWorldTransformCommand& command)
                {
                    log.push_back("SetWorldTransform:" + std::to_string(command.entity.id));
                    transforms[command.entity.id] = command.worldTransform;
                    worlds.erase(command.entity.id);
                });

            d.registerQueryHandler<GetMeshDataQuery>(
                [this](const GetMeshDataQuery& query) -> std::optional<services::MeshData>
                {
                    const auto it = meshes.find(query.entity.id);
                    return it != meshes.end() ? std::optional<services::MeshData>(it->second) : std::nullopt;
                });
            d.registerQueryHandler<events::animationAuthoring::GetMeshSkeletonQuery>(
                [this](const events::animationAuthoring::GetMeshSkeletonQuery& query) -> services::MeshSkeletonInfo
                {
                    const auto it = skeletons.find(query.meshPath);
                    if (it != skeletons.end())
                    {
                        return it->second;
                    }
                    services::MeshSkeletonInfo missing;
                    missing.error = "cannot open " + query.meshPath;
                    return missing;
                });

            d.registerQueryHandler<GetSocketNamesQuery>(
                [this](const GetSocketNamesQuery& query)
                {
                    const auto it = sockets.find(query.entity.id);
                    return it != sockets.end() ? it->second : std::vector<std::string>{};
                });
            d.registerQueryHandler<GetSocketWorldTransformQuery>(
                [this](const GetSocketWorldTransformQuery& query)
                {
                    const auto it = socketWorlds.find({query.parentEntity.id, query.socketName});
                    return it != socketWorlds.end() ? it->second : glm::mat4(1.0f);
                });
            // SocketAdapter::getSocketAttachmentData: an unresolved parent is an invalid handle.
            d.registerQueryHandler<GetSocketAttachmentDataQuery>(
                [this](const GetSocketAttachmentDataQuery& query) -> std::optional<SocketAttachmentData>
                {
                    const auto it = links.find(query.entity.id);
                    if (it == links.end())
                    {
                        return std::nullopt;
                    }
                    SocketAttachmentData data;
                    const bool resolved = it->second.parent.has_value() && alive(*it->second.parent);
                    data.parentEntity = resolved ? handleOf(*it->second.parent) : services::EntityHandle::invalid();
                    data.parentEntityName = resolved ? entities.at(*it->second.parent).name : std::string();
                    data.socketName = it->second.socket;
                    data.isActive = it->second.active;
                    return data;
                });
            d.registerQueryHandler<HasSocketAttachmentComponentQuery>(
                [this](const HasSocketAttachmentComponentQuery& query) { return links.contains(query.entity.id); });
            // SocketAdapter::attachToSocket: needs the socket on the parent, reuses a component, re-activates it
            // and moves the child onto the socket.
            d.registerCommandHandler<AttachToSocketCommand>(
                [this](const AttachToSocketCommand& command)
                {
                    log.push_back("Attach:" + std::to_string(command.childEntity.id) + "->" +
                                  std::to_string(command.parentEntity.id) + "." + command.socketName);
                    if (!alive(command.childEntity.id) || !alive(command.parentEntity.id))
                    {
                        return false;
                    }
                    const auto names = sockets.find(command.parentEntity.id);
                    if (names == sockets.end() ||
                        std::find(names->second.begin(), names->second.end(), command.socketName) == names->second.end())
                    {
                        return false;
                    }
                    links[command.childEntity.id] = Link{command.parentEntity.id, command.socketName, true};
                    const auto position = socketPositions.find({command.parentEntity.id, command.socketName});
                    transforms[command.childEntity.id].position =
                        position != socketPositions.end() ? position->second : glm::vec3(9.0f);
                    return true;
                });
            // SocketAdapter::detachFromSocket: removes the component and restores nothing.
            d.registerCommandHandler<DetachFromSocketCommand>(
                [this](const DetachFromSocketCommand& command)
                {
                    log.push_back("Detach:" + std::to_string(command.childEntity.id));
                    links.erase(command.childEntity.id);
                });
            d.registerCommandHandler<SetSocketActiveCommand>(
                [this](const SetSocketActiveCommand& command)
                {
                    log.push_back("SetActive:" + std::to_string(command.entity.id) + "=" + (command.active ? "1" : "0"));
                    if (const auto it = links.find(command.entity.id); it != links.end())
                    {
                        it->second.active = command.active;
                    }
                });
            d.registerCommandHandler<AddSocketAttachmentComponentCommand>(
                [this](const AddSocketAttachmentComponentCommand& command)
                {
                    log.push_back("AddComponent:" + std::to_string(command.entity.id));
                    if (!alive(command.entity.id) || links.contains(command.entity.id))
                    {
                        return false;
                    }
                    links[command.entity.id] = Link{};
                    return true;
                });

            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command)
                {
                    log.push_back("PushUndo");
                    undoStack.push_back(command.command);
                });
        }
    };

    struct SocketToolFixture
    {
        DispatcherScope dispatcherScope;
        FakeSocketScene scene;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        SocketToolFixture()
        {
            scene.registerHandlers();
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerAnimationSocketTools(registry, context);
        }

        mcp::ToolResult call(const char* name, const nlohmann::json& args)
        {
            auto tool = registry.find(name);
            REQUIRE(tool != nullptr);
            return tool->handler(args);
        }

        nlohmann::json callOk(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolResult result = call(name, args);
            INFO(name << " failed: " << result.text);
            REQUIRE_FALSE(result.isError);
            return result.structured;
        }

        std::string error(const char* name, const nlohmann::json& args)
        {
            try
            {
                call(name, args);
            }
            catch (const std::exception& e)
            {
                return e.what();
            }
            return "<no error>";
        }

        nlohmann::json attach(uint64_t child, uint64_t parent, const char* socket)
        {
            return callOk("animation_attach_to_socket", {{"child", child}, {"parent", parent}, {"socket", socket}});
        }
    };
}

TEST_SUITE("McpAnimationSocketTools")
{
    TEST_CASE("registering the socket tools dispatches nothing; four Main-affinity tools with closed schemas")
    {
        DispatcherScope scope; // no handlers at all: any dispatch would throw
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;
        const mcp::tools::ToolContext context{queue};
        CHECK_NOTHROW(mcp::tools::registerAnimationSocketTools(registry, context));
        CHECK(registry.size() == 4);

        struct Expected
        {
            const char* name;
            bool readOnly;
            bool destructive;
        };
        for (const Expected& entry : {Expected{"animation_list_sockets", true, false},
                                      Expected{"animation_attach_to_socket", false, false},
                                      Expected{"animation_detach_from_socket", false, true},
                                      Expected{"animation_set_socket_active", false, false}})
        {
            CAPTURE(entry.name);
            const auto tool = registry.find(entry.name);
            REQUIRE(tool != nullptr);
            CHECK(tool->affinity == mcp::ThreadAffinity::Main);
            CHECK(tool->readOnly == entry.readOnly);
            CHECK(tool->destructive == entry.destructive);
            CHECK(tool->inputSchema["additionalProperties"] == false);
        }
        CHECK(registry.find("animation_attach_to_socket")->inputSchema["required"] ==
              nlohmann::json::array({"child", "parent", "socket"}));
    }

    TEST_CASE("animation_attach_to_socket refuses self, loops, unknown sockets and the root before anything changes")
    {
        SocketToolFixture f;
        auto attachError = [&f](uint64_t child, uint64_t parent, const char* socket)
        {
            return f.error("animation_attach_to_socket", {{"child", child}, {"parent", parent}, {"socket", socket}});
        };

        CHECK_THROWS_AS((f.call("animation_attach_to_socket", {{"child", swordId}, {"parent", swordId}, {"socket", "x"}})),
                        mcp::ArgError);
        CHECK(contains(attachError(rootId, heroId, "hand_r"), "The scene root cannot be attached"));
        CHECK(contains(attachError(terrainId, heroId, "hand_r"), "it is a terrain"));

        // The gem sits below the sword: the sword would follow a socket that moves with the sword.
        const std::string below = attachError(swordId, gemId, "top");
        CHECK(contains(below, "the parent already follows the child"));
        CHECK(contains(below, "'Gem' (16) -> 'Sword' (11) [hierarchy parent]"));

        // The hero already follows the crate's socket: attaching the crate to the hero closes the loop.
        f.scene.links[heroId] = FakeSocketScene::Link{crateId, "top", true};
        const std::string loop = attachError(crateId, heroId, "hand_r");
        CHECK(contains(loop, "'Hero' (10) -> 'Crate' (13) [socket parent]"));

        CHECK(contains(attachError(swordId, heroId, "tail"), "has no socket 'tail'; its sockets: hand_r, head, ghost"));
        CHECK(contains(attachError(swordId, shieldId, "hand_r"), "has no sockets"));
        CHECK_THROWS_AS((f.call("animation_attach_to_socket", {{"child", swordId}, {"parent", heroId}, {"socket", ""}})),
                        mcp::ArgError);
        CHECK_THROWS_AS((f.call("animation_attach_to_socket", {{"child", swordId}, {"parent", heroId}, {"socket", "hand_r"},
                                                               {"bone", "Hand_R"}})),
                        mcp::ArgError);
        CHECK(contains(attachError(swordId, 999, "hand_r"), "does not exist"));

        CHECK(f.scene.log.empty());
        CHECK(f.scene.undoStack.empty());
        CHECK_FALSE(f.scene.links.contains(swordId));
    }

    TEST_CASE("attaching a free entity: undo detaches and puts the transform back, redo re-attaches")
    {
        SocketToolFixture f;
        const services::TransformData start = transformAt({5.0f, 0.0f, 0.0f}, {0.0f, 45.0f, 0.0f});
        f.scene.transforms[swordId] = start;

        const nlohmann::json out = f.attach(swordId, heroId, "hand_r");

        checkLog(f.scene.log, {"Attach:11->10.hand_r", "PushUndo"});
        CHECK(out["changed"] == true);
        CHECK(out["active"] == true);
        CHECK(out["previous"] == nlohmann::json{{"state", "none"}});
        CHECK(out["warnings"].empty());
        REQUIRE(out["socketWorld"].is_object());
        CHECK(out["socketWorld"]["position"] == nlohmann::json::array({1.0, 2.0, 3.0}));
        CHECK(out["socketWorld"]["rotationEuler"][0].get<double>() == doctest::Approx(0.0));
        CHECK(out["socketWorld"]["rotationEuler"][1].get<double>() == doctest::Approx(30.0));
        CHECK(out["socketWorld"]["rotationEuler"][2].get<double>() == doctest::Approx(0.0));

        REQUIRE(f.scene.undoStack.size() == 1);
        const auto entry = f.scene.undoStack.back();
        CHECK(entry->getDescription() == "MCP: Attach to socket (Sword -> Hero.hand_r)");
        CHECK(entry->getMemoryFootprint() > 0);
        const services::TransformData onSocket = f.scene.transforms.at(swordId);
        CHECK(onSocket.position == glm::vec3(1.0f, 2.0f, 3.0f));

        f.scene.log.clear();
        entry->undo();
        checkLog(f.scene.log, {"Detach:11", "SetTransform:11"});
        CHECK_FALSE(f.scene.links.contains(swordId));
        CHECK(f.scene.transforms.at(swordId) == start);

        f.scene.log.clear();
        entry->execute();
        checkLog(f.scene.log, {"Attach:11->10.hand_r", "SetActive:11=1", "SetTransform:11"});
        REQUIRE(f.scene.links.contains(swordId));
        CHECK(f.scene.links.at(swordId).parent == heroId);
        CHECK(f.scene.links.at(swordId).socket == "hand_r");
        CHECK(f.scene.transforms.at(swordId) == onSocket);
    }

    TEST_CASE("attach undo restores an empty component, or the previous link with its active state")
    {
        SocketToolFixture f;
        const services::TransformData start = transformAt({5.0f, 0.0f, 0.0f});
        f.scene.transforms[swordId] = start;

        SUBCASE("from an empty component")
        {
            f.scene.links[swordId] = FakeSocketScene::Link{};
            const nlohmann::json out = f.attach(swordId, heroId, "hand_r");
            CHECK(out["previous"] == nlohmann::json{{"state", "empty"}});

            f.scene.log.clear();
            f.scene.undoStack.back()->undo();
            checkLog(f.scene.log, {"Detach:11", "AddComponent:11", "SetTransform:11"});
            REQUIRE(f.scene.links.contains(swordId));
            CHECK_FALSE(f.scene.links.at(swordId).parent.has_value());
            CHECK(f.scene.links.at(swordId).socket.empty());
            CHECK(f.scene.transforms.at(swordId) == start);
        }

        SUBCASE("from an inactive link to another parent")
        {
            f.scene.links[swordId] = FakeSocketScene::Link{crateId, "top", false};
            const nlohmann::json out = f.attach(swordId, heroId, "hand_r");
            CHECK(out["previous"] == nlohmann::json{{"state", "attached"}, {"parent", crateId}, {"parentName", "Crate"},
                                                    {"parentResolved", true}, {"socket", "top"}, {"active", false}});

            f.scene.log.clear();
            f.scene.undoStack.back()->undo();
            checkLog(f.scene.log, {"Attach:11->13.top", "SetActive:11=0", "SetTransform:11"});
            CHECK(f.scene.links.at(swordId).parent == crateId);
            CHECK(f.scene.links.at(swordId).socket == "top");
            CHECK_FALSE(f.scene.links.at(swordId).active);
            CHECK(f.scene.transforms.at(swordId) == start);
        }

        SUBCASE("active=false switches the new link off after attaching")
        {
            const nlohmann::json out = f.callOk("animation_attach_to_socket",
                                                {{"child", swordId}, {"parent", heroId}, {"socket", "hand_r"}, {"active", false}});
            CHECK(out["active"] == false);
            checkLog(f.scene.log, {"Attach:11->10.hand_r", "SetActive:11=0", "PushUndo"});
            CHECK_FALSE(f.scene.links.at(swordId).active);
        }

        SUBCASE("the same link again changes nothing")
        {
            f.scene.links[swordId] = FakeSocketScene::Link{heroId, "hand_r", true};
            const nlohmann::json out = f.attach(swordId, heroId, "hand_r");
            CHECK(out["changed"] == false);
            CHECK(f.scene.log.empty());
            CHECK(f.scene.undoStack.empty());
        }

        SUBCASE("a static parent's socket world transform may not be known yet")
        {
            const nlohmann::json out = f.attach(swordId, crateId, "top");
            CHECK(out["socketWorld"].is_null());
        }
    }

    TEST_CASE("attach warns when the parent's name is shared")
    {
        SocketToolFixture f;
        f.scene.addEntity(15, "Hero", rootId);

        const nlohmann::json out = f.attach(swordId, heroId, "hand_r");
        CHECK(anyContains(out["warnings"], "2 entities are named 'Hero'"));
        CHECK(f.scene.links.contains(swordId));
    }

    TEST_CASE("attach undo / redo follow a parent that came back under a new id")
    {
        SocketToolFixture f;
        f.attach(swordId, heroId, "hand_r");
        const auto entry = f.scene.undoStack.back();
        entry->undo();

        f.scene.moveEntity(heroId, 20);
        mcp::undo::EntityIdRemap::instance().record(static_cast<uint32_t>(heroId), 20);
        f.scene.log.clear();
        entry->execute();
        checkLog(f.scene.log, {"Attach:11->20.hand_r", "SetActive:11=1", "SetTransform:11"});
        CHECK(f.scene.links.at(swordId).parent == 20);

        // The parent is gone for good: the entry throws inside the batch, which logs it and is consumed.
        entry->undo();
        f.scene.entities.erase(20);
        f.scene.log.clear();
        CHECK_NOTHROW(entry->execute());
        CHECK(f.scene.log.empty());
    }

    TEST_CASE("animation_detach_from_socket keeps the world pose and is undone exactly")
    {
        SocketToolFixture f;
        const services::TransformData onSocket = transformAt({1.0f, 2.0f, 3.0f});
        const services::TransformData world = transformAt({1.0f, 2.0f, 3.0f}, {0.0f, 30.0f, 0.0f});
        f.scene.links[swordId] = FakeSocketScene::Link{heroId, "hand_r", true};
        f.scene.transforms[swordId] = onSocket;
        f.scene.worlds[swordId] = world;

        SUBCASE("keepWorldPose (default)")
        {
            const nlohmann::json out = f.callOk("animation_detach_from_socket", {{"child", swordId}});
            checkLog(f.scene.log, {"Detach:11", "SetWorldTransform:11", "PushUndo"});
            CHECK(out["keepWorldPose"] == true);
            CHECK(out["detached"]["parent"] == heroId);
            CHECK(out["detached"]["socket"] == "hand_r");
            CHECK(out["transform"]["rotation"] == nlohmann::json::array({0.0, 30.0, 0.0}));
            CHECK(f.scene.transforms.at(swordId) == world);

            const auto entry = f.scene.undoStack.back();
            CHECK(entry->getDescription() == "MCP: Detach from socket (Sword)");
            f.scene.log.clear();
            entry->undo();
            checkLog(f.scene.log, {"Attach:11->10.hand_r", "SetActive:11=1", "SetTransform:11"});
            CHECK(f.scene.links.at(swordId).parent == heroId);
            CHECK(f.scene.transforms.at(swordId) == onSocket);

            f.scene.log.clear();
            entry->execute();
            checkLog(f.scene.log, {"Detach:11", "SetTransform:11"});
            CHECK_FALSE(f.scene.links.contains(swordId));
            CHECK(f.scene.transforms.at(swordId) == world);
        }

        SUBCASE("keepWorldPose=false leaves the engine's transform")
        {
            f.callOk("animation_detach_from_socket", {{"child", swordId}, {"keepWorldPose", false}});
            checkLog(f.scene.log, {"Detach:11", "PushUndo"});
            CHECK(f.scene.transforms.at(swordId) == onSocket);
        }

        SUBCASE("an inactive link comes back inactive")
        {
            f.scene.links[swordId].active = false;
            f.callOk("animation_detach_from_socket", {{"child", swordId}});
            f.scene.log.clear();
            f.scene.undoStack.back()->undo();
            checkLog(f.scene.log, {"Attach:11->10.hand_r", "SetActive:11=0", "SetTransform:11"});
            CHECK_FALSE(f.scene.links.at(swordId).active);
        }
    }

    TEST_CASE("detach and set_active need an attachment")
    {
        SocketToolFixture f;
        CHECK(contains(f.error("animation_detach_from_socket", {{"child", swordId}}), "is not attached to a socket"));
        CHECK(contains(f.error("animation_set_socket_active", {{"child", swordId}, {"active", false}}),
                       "is not attached to a socket"));
        f.scene.links[swordId] = FakeSocketScene::Link{};
        CHECK(contains(f.error("animation_detach_from_socket", {{"child", swordId}}),
                       "has an empty SocketAttachment component"));
        CHECK(contains(f.error("animation_set_socket_active", {{"child", swordId}, {"active", false}}),
                       "is not attached to a socket"));
        CHECK(f.scene.log.empty());
        CHECK(f.scene.undoStack.empty());
    }

    TEST_CASE("animation_set_socket_active is undoable and restores the transform it left")
    {
        SocketToolFixture f;
        const services::TransformData frozen = transformAt({4.0f, 0.0f, 0.0f});
        f.scene.links[swordId] = FakeSocketScene::Link{heroId, "hand_r", false};
        f.scene.transforms[swordId] = frozen;

        const nlohmann::json out = f.callOk("animation_set_socket_active", {{"child", swordId}, {"active", true}});
        checkLog(f.scene.log, {"SetActive:11=1", "PushUndo"});
        CHECK(out["changed"] == true);
        CHECK(out["attachment"]["active"] == true);

        const auto entry = f.scene.undoStack.back();
        CHECK(entry->getDescription() == "MCP: Activate socket attachment (Sword)");
        f.scene.transforms[swordId] = transformAt({1.0f, 2.0f, 3.0f}); // the socket moved it
        f.scene.log.clear();
        entry->undo();
        checkLog(f.scene.log, {"SetActive:11=0", "SetTransform:11"});
        CHECK_FALSE(f.scene.links.at(swordId).active);
        CHECK(f.scene.transforms.at(swordId) == frozen);

        f.scene.log.clear();
        entry->execute();
        checkLog(f.scene.log, {"SetActive:11=1"});
        CHECK(f.scene.links.at(swordId).active);

        f.scene.log.clear();
        const nlohmann::json same = f.callOk("animation_set_socket_active", {{"child", swordId}, {"active", true}});
        CHECK(same["changed"] == false);
        CHECK(f.scene.log.empty());
    }

    TEST_CASE("an unresolved parent is reported as null and its link can still be detached")
    {
        SocketToolFixture f;
        f.scene.links[swordId] = FakeSocketScene::Link{999, "hand_r", true};

        const nlohmann::json listed = f.callOk("animation_list_sockets", {{"entity", swordId}});
        CHECK(listed["attachment"] == nlohmann::json{{"state", "attached"}, {"parent", nullptr}, {"parentName", nullptr},
                                                     {"parentResolved", false}, {"socket", "hand_r"}, {"active", true}});
        CHECK(anyContains(listed["warnings"], "of a parent that is not resolved"));
        CHECK(anyContains(listed["warnings"], "has no mesh"));
        CHECK(listed["skinned"].is_null());
        CHECK(listed["sockets"].empty());

        // Nothing may claim the unresolved child.
        const nlohmann::json hero = f.callOk("animation_list_sockets", {{"entity", heroId}});
        CHECK(hero["attachedChildren"].empty());

        const nlohmann::json out = f.callOk("animation_detach_from_socket", {{"child", swordId}});
        CHECK(out["detached"]["parent"].is_null());
        CHECK(out["detached"]["parentResolved"] == false);
        CHECK(anyContains(out["warnings"], "undo restores an empty SocketAttachment component"));

        f.scene.log.clear();
        f.scene.undoStack.back()->undo();
        checkLog(f.scene.log, {"AddComponent:11", "SetTransform:11"});
        REQUIRE(f.scene.links.contains(swordId));
        CHECK(f.scene.links.at(swordId).socket.empty());
    }

    TEST_CASE("animation_list_sockets reads the mesh's sockets and the entities attached to them")
    {
        SocketToolFixture f;
        f.scene.links[swordId] = FakeSocketScene::Link{heroId, "hand_r", true};
        f.scene.links[shieldId] = FakeSocketScene::Link{heroId, "gone", false};
        f.scene.links[gemId] = FakeSocketScene::Link{crateId, "top", true};

        const nlohmann::json out = f.callOk("animation_list_sockets", {{"entity", heroId}});

        REQUIRE(out["mesh"].is_string());
        CHECK(contains(out["mesh"].get<std::string>(), "characters/Hero.vfMesh"));
        CHECK(out["skinned"] == true);
        CHECK(out["hasAnimator"] == true);
        REQUIRE(out["sockets"].size() == 3);
        const nlohmann::json& hand = out["sockets"][0];
        CHECK(hand["name"] == "hand_r");
        CHECK(hand["bone"] == "Hand_R");
        CHECK(hand["boneResolved"] == true);
        CHECK(hand["position"] == nlohmann::json::array({0.1, 0.0, 0.0}));
        CHECK(hand["rotationEuler"][0].get<double>() == doctest::Approx(45.0));
        CHECK(hand["rotationEuler"][1].get<double>() == doctest::Approx(0.0));
        CHECK(hand["rotationEuler"][2].get<double>() == doctest::Approx(0.0));
        CHECK(out["sockets"][2]["boneResolved"] == false);
        CHECK(anyContains(out["warnings"], "Socket 'ghost' names bone 'Tail'"));

        const nlohmann::json& children = out["attachedChildren"];
        REQUIRE(children.size() == 2);
        CHECK(children[0] == nlohmann::json{{"entity", swordId}, {"name", "Sword"}, {"socket", "hand_r"}, {"active", true},
                                            {"socketFound", true}});
        CHECK(children[1]["entity"] == shieldId);
        CHECK(children[1]["socketFound"] == false);
        CHECK(anyContains(out["warnings"], "is attached to socket 'gone', which this mesh does not define"));
        CHECK(out["attachment"] == nlohmann::json{{"state", "none"}});

        SUBCASE("a static mesh")
        {
            const nlohmann::json crate = f.callOk("animation_list_sockets", {{"entity", crateId}});
            CHECK(crate["skinned"] == false);
            REQUIRE(crate["sockets"].size() == 1);
            CHECK(crate["sockets"][0] == nlohmann::json{{"name", "top"}, {"bone", nullptr}, {"boneResolved", nullptr},
                                                        {"position", {0.0, 1.0, 0.0}}, {"rotationEuler", {0.0, 0.0, 0.0}}});
            REQUIRE(crate["attachedChildren"].size() == 1);
            CHECK(crate["attachedChildren"][0]["entity"] == gemId);
            CHECK(crate["warnings"].empty());
        }
    }
}
