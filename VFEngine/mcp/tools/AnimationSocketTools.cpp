#include "AnimationRigSupport.hpp"
#include "CoreTools.hpp"
#include "SettingsTable.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/SocketUndo.hpp"

#include "events/EventDispatcher.hpp"
#include "events/physics/SocketEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "math/TransformUtils.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// VK-1656 socket tools: list the sockets of an entity's mesh and attach entities to them
// (SocketAttachmentComponent). Attaching does not reparent: the socket drives the child's WORLD pose every
// frame, in edit and Play mode, and the child's local rotation and scale become offsets from the socket.
// Like the entity tools they work in Play mode (Stop restores the scene snapshot). Each edit pushes one
// undo step (mcp/undo/SocketUndo) that also restores the child's local transform.
namespace mcp::tools
{
    namespace
    {
        using AttachmentData = events::socket::SocketAttachmentData;
        using SocketState = undo::SocketAttachmentState;

        constexpr const char* terrainHint = "A terrain and its tiles cannot follow a socket.";

        std::string entityLabel(const services::EntityData& entity)
        {
            return "Entity " + std::to_string(toId(entity.handle)) + " ('" + entity.name + "')";
        }

        std::string handleLabel(const services::EntityHandle& handle)
        {
            const std::optional<services::EntityData> data = findEntity(handle);
            return (data.has_value() ? "'" + data->name + "'" : std::string("entity")) + " (" +
                   std::to_string(handle.id) + ")";
        }

        std::string joined(const std::vector<std::string>& names)
        {
            std::string out;
            for (const std::string& name : names)
            {
                out += (out.empty() ? "" : ", ") + name;
            }
            return out.empty() ? std::string("none") : out;
        }

        std::string warningCount(const nlohmann::json& warnings)
        {
            return warnings.empty() ? std::string() : " " + std::to_string(warnings.size()) + " warning(s).";
        }

        // A component that names a socket. Its parent may be unresolved (an invalid handle).
        bool isLinked(const std::optional<AttachmentData>& data)
        {
            return data.has_value() && !data->socketName.empty();
        }

        // {state: none | empty | attached, parent (null while unresolved), parentName, parentResolved, socket, active}.
        nlohmann::json attachmentJson(const std::optional<AttachmentData>& data)
        {
            if (!data.has_value())
            {
                return {{"state", "none"}};
            }
            if (data->socketName.empty())
            {
                return {{"state", "empty"}};
            }
            const std::optional<services::EntityData> parent =
                data->parentEntity.isValid() ? findEntity(data->parentEntity) : std::nullopt;
            return {
                {"state", "attached"},
                {"parent", parent.has_value() ? entityId(parent->handle) : nlohmann::json(nullptr)},
                {"parentName", parent.has_value() ? nlohmann::json(parent->name) : nlohmann::json(nullptr)},
                {"parentResolved", parent.has_value()},
                {"socket", data->socketName},
                {"active", data->isActive}
            };
        }

        // The child's attachment as the undo entry can restore it, plus its local transform. A link whose
        // parent is unresolved cannot be re-created through the socket API: it is recorded as an empty
        // component (unrestorableLinkWarning tells the agent).
        SocketState captureState(const services::EntityHandle& child, const std::optional<AttachmentData>& data)
        {
            SocketState state;
            if (data.has_value())
            {
                state.kind = SocketState::Kind::Empty;
                if (!data->socketName.empty() && rig::isLiveEntity(data->parentEntity))
                {
                    state.kind = SocketState::Kind::Attached;
                    state.parent = toId(data->parentEntity);
                    state.socket = data->socketName;
                    state.active = data->isActive;
                }
            }
            state.transform = rig::localTransform(child);
            return state;
        }

        std::optional<std::string> unrestorableLinkWarning(const std::optional<AttachmentData>& data)
        {
            if (!isLinked(data) || rig::isLiveEntity(data->parentEntity))
            {
                return std::nullopt;
            }
            return "The previous link to socket '" + data->socketName + "' had an unresolved parent; undo restores "
                   "an empty SocketAttachment component instead of that link.";
        }

        // GetSocketWorldTransformQuery answers identity when it cannot place the socket (e.g. a skinned
        // parent whose animator has not run yet): reported as null.
        nlohmann::json socketWorldJson(const glm::mat4& world)
        {
            if (world == glm::mat4(1.0f))
            {
                return nullptr;
            }
            const math::DecomposedTransform decomposed = math::decomposeMatrix(world);
            return {
                {"position", env::vec3Json(decomposed.position)},
                {"rotationEuler", rig::degreesJson(decomposed.rotation)}
            };
        }

        // Everything the world pose of `start` follows, transitively: its hierarchy parents and, while it is
        // attached, its socket parent. When `target` is among them, the chain from `start` to it as text.
        std::optional<std::string> followChain(const services::EntityHandle& start, const services::EntityHandle& target)
        {
            struct Step
            {
                uint64_t previous;
                const char* via;
            };
            std::unordered_map<uint64_t, Step> reached{{start.id, Step{start.id, nullptr}}};
            std::deque<services::EntityHandle> pending{start};
            while (!pending.empty())
            {
                const services::EntityHandle current = pending.front();
                pending.pop_front();
                if (current == target)
                {
                    std::vector<std::string> steps;
                    for (uint64_t id = current.id; id != start.id; id = reached.at(id).previous)
                    {
                        steps.push_back(handleLabel(services::EntityHandle{id}) + " [" + reached.at(id).via + "]");
                    }
                    std::string text = handleLabel(start);
                    for (auto it = steps.rbegin(); it != steps.rend(); ++it)
                    {
                        text += " -> " + *it;
                    }
                    return text;
                }

                const std::optional<services::EntityData> data = findEntity(current);
                if (!data.has_value())
                {
                    continue;
                }
                const auto visit = [&](const services::EntityHandle& next, const char* via)
                {
                    if (next.isValid() && reached.emplace(next.id, Step{current.id, via}).second)
                    {
                        pending.push_back(next);
                    }
                };
                if (data->parent.has_value())
                {
                    visit(*data->parent, "hierarchy parent");
                }
                const std::optional<AttachmentData> link = rig::socketAttachment(current);
                if (isLinked(link) && rig::isLiveEntity(link->parentEntity))
                {
                    visit(link->parentEntity, "socket parent");
                }
            }
            return std::nullopt;
        }

        nlohmann::json socketJson(const animator::SocketDefinition& socket, bool skinned)
        {
            return {
                {"name", socket.name},
                {"bone", skinned && !socket.targetBoneName.empty() ? nlohmann::json(socket.targetBoneName)
                                                                   : nlohmann::json(nullptr)},
                {"boneResolved", skinned ? nlohmann::json(socket.boneIndex >= 0) : nlohmann::json(nullptr)},
                {"position", env::vec3Json(socket.localPosition)},
                {"rotationEuler", rig::degreesJson(rig::eulerDegrees(socket.localRotation))}
            };
        }

        // ------------------------------------------------------------------
        // Tools
        // ------------------------------------------------------------------

        void registerListSockets(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_list_sockets";
            tool.title = "List sockets";
            tool.description =
                "List the sockets of an entity's mesh, read fresh from the .vfMesh: [{name, bone (skinned meshes; "
                "null for a static mesh), boneResolved, position, rotationEuler (DEGREES)}] - offsets from the bone "
                "(skinned) or from the mesh origin (static). Also returns attachedChildren, the entities attached to "
                "these sockets [{entity, name, socket, active, socketFound}], and this entity's own attachment "
                "{state none|empty|attached, parent (null while unresolved), parentName, parentResolved, socket, "
                "active}. Sockets are authored in the Mesh / Animation preview window. Works in Play mode.";
            tool.inputSchema = closedObject({{"entity", schema::entity()}}, {"entity"});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity"}, "animation_list_sockets");
                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                auto& dispatcher = events::EventDispatcher::instance();

                nlohmann::json warnings = nlohmann::json::array();
                nlohmann::json sockets = nlohmann::json::array();
                std::optional<std::vector<std::string>> socketNames; // nullopt: the mesh could not be read
                nlohmann::json skinned = nullptr;

                const std::optional<services::MeshData> mesh = rig::meshData(entity.handle);
                const bool hasAnimator = mesh.has_value() && mesh->animatorRef.isValid();
                if (!mesh.has_value() || !mesh->meshRef.isValid())
                {
                    warnings.push_back(entityLabel(entity) + " has no mesh: sockets are defined by a .vfMesh.");
                }
                else if (const std::string path = rig::meshPath(*mesh); path.empty())
                {
                    warnings.push_back("The mesh asset (guid:" + mesh->meshRef.toHexString() + ") cannot be resolved.");
                }
                else
                {
                    const std::string meshLabel = "'" + rig::agentPath(path) + "'";
                    const services::MeshSkeletonInfo skeleton = rig::readSkeleton(path);
                    if (!skeleton.success)
                    {
                        warnings.push_back("Cannot read the sockets of " + meshLabel + ": " + skeleton.error);
                    }
                    else
                    {
                        skinned = skeleton.skinned;
                        socketNames.emplace();
                        for (const animator::SocketDefinition& socket : skeleton.sockets)
                        {
                            sockets.push_back(socketJson(socket, skeleton.skinned));
                            socketNames->push_back(socket.name);
                            if (skeleton.skinned && socket.boneIndex < 0)
                            {
                                warnings.push_back("Socket '" + socket.name + "' names bone '" + socket.targetBoneName +
                                                   "', which the skeleton of " + meshLabel + " lacks.");
                            }
                        }
                        if (skeleton.sockets.empty())
                        {
                            warnings.push_back(meshLabel + " defines no sockets (author them in the Mesh / Animation "
                                               "preview window).");
                        }
                        else if (skeleton.skinned && !hasAnimator)
                        {
                            warnings.push_back(entityLabel(entity) + " has no Animator: its sockets stay at their "
                                               "bind-pose places and attached entities do not follow any animation.");
                        }
                    }
                }

                nlohmann::json children = nlohmann::json::array();
                events::scene::GetEntitiesWithComponentQuery attachedQuery;
                attachedQuery.componentType = services::ComponentTypeId::SocketAttachment;
                for (const services::EntityHandle& child : dispatcher.query(attachedQuery))
                {
                    const std::optional<AttachmentData> link = rig::socketAttachment(child);
                    if (!isLinked(link) || link->parentEntity != entity.handle)
                    {
                        continue;
                    }
                    const std::optional<services::EntityData> childData = findEntity(child);
                    nlohmann::json socketFound = nullptr;
                    if (socketNames.has_value())
                    {
                        const bool found = std::find(socketNames->begin(), socketNames->end(), link->socketName) !=
                                           socketNames->end();
                        socketFound = found;
                        if (!found)
                        {
                            warnings.push_back(handleLabel(child) + " is attached to socket '" + link->socketName +
                                               "', which this mesh does not define: it does not follow.");
                        }
                    }
                    children.push_back({
                        {"entity", entityId(child)},
                        {"name", childData.has_value() ? nlohmann::json(childData->name) : nlohmann::json(nullptr)},
                        {"socket", link->socketName},
                        {"active", link->isActive},
                        {"socketFound", socketFound}
                    });
                }

                const std::optional<AttachmentData> own = rig::socketAttachment(entity.handle);
                if (isLinked(own) && !rig::isLiveEntity(own->parentEntity))
                {
                    warnings.push_back(entityLabel(entity) + " is linked to socket '" + own->socketName +
                                       "' of a parent that is not resolved: it does not follow any socket.");
                }

                const std::size_t socketCount = sockets.size();
                const std::size_t childCount = children.size();
                nlohmann::json out{
                    {"entity", entityId(entity.handle)},
                    {"name", entity.name},
                    {"mesh", mesh.has_value() ? rig::meshJson(*mesh) : nlohmann::json(nullptr)},
                    {"skinned", skinned},
                    {"hasAnimator", hasAnimator},
                    {"sockets", std::move(sockets)},
                    {"attachedChildren", std::move(children)},
                    {"attachment", attachmentJson(own)},
                    {"warnings", warnings}
                };
                std::string summary = entityLabel(entity) + ": " + std::to_string(socketCount) + " socket(s)";
                if (socketNames.has_value() && !socketNames->empty())
                {
                    summary += " (" + joined(*socketNames) + ")";
                }
                summary += ", " + std::to_string(childCount) + " attached entit" + (childCount == 1 ? "y." : "ies.");
                summary += warningCount(warnings);
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        void registerAttachToSocket(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_attach_to_socket";
            tool.title = "Attach to socket";
            tool.description =
                "Attach 'child' to socket 'socket' of 'parent' (an entity with a mesh; animation_list_sockets lists "
                "its sockets). The child is NOT reparented: the socket drives its WORLD pose every frame, in edit "
                "and Play mode, its position is overwritten, and its local rotation and scale become offsets from "
                "the socket. A skinned parent's socket follows its bone while the parent's Animator runs. "
                "active=false stores the link switched off: attaching still moves the child onto the socket once, "
                "and it stays there until the link is switched on (animation_set_socket_active). "
                "Refused when the parent already follows the child (it is below the child in the hierarchy, or a "
                "chain of socket links leads back to the child). The link is stored by entity UUID and, after a "
                "prefab / JSON round trip (an undone entity_delete, entity_instantiate_json), re-resolved by the "
                "parent's NAME: keep parent names unique. One undo step (restores the previous attachment and the "
                "child's transform). Returns {changed, previous, socketWorld {position, rotationEuler} (null when "
                "not known yet, e.g. before the parent's animator has run), warnings}.";
            tool.inputSchema = closedObject({
                {"child", schema::entity("The entity to attach")},
                {"parent", schema::entity("The entity whose mesh has the socket")},
                {"socket", schema::string("Socket name (animation_list_sockets)")},
                {"active", schema::boolean("Follow the socket now. Default true.")}
            }, {"child", "parent", "socket"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"child", "parent", "socket", "active"}, "animation_attach_to_socket");
                const ArgReader reader(args);
                const services::EntityData child = requireEntity(reader, "child");
                const services::EntityData parent = requireEntity(reader, "parent");
                const std::string socket = reader.requireString("socket");
                const bool active = reader.optBool("active", true);
                if (socket.empty())
                {
                    throw ArgError("argument 'socket' must not be empty");
                }
                if (child.handle == parent.handle)
                {
                    throw ArgError("an entity cannot be attached to its own socket");
                }
                if (!child.parent.has_value() || !child.parent->isValid())
                {
                    throw std::runtime_error("The scene root cannot be attached to a socket.");
                }
                rejectTerrain(child, "attach", terrainHint);
                if (const std::optional<std::string> loop = followChain(parent.handle, child.handle))
                {
                    throw std::runtime_error("Cannot attach " + entityLabel(child) + " to a socket of " +
                                             entityLabel(parent) + ": the parent already follows the child (" + *loop +
                                             "), so the socket would end up driving its own parent.");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                events::socket::GetSocketNamesQuery namesQuery;
                namesQuery.entity = parent.handle;
                const std::vector<std::string> sockets = dispatcher.query(namesQuery);
                if (std::find(sockets.begin(), sockets.end(), socket) == sockets.end())
                {
                    throw std::runtime_error(sockets.empty()
                        ? entityLabel(parent) + " has no sockets (its mesh defines none; animation_list_sockets shows them)."
                        : entityLabel(parent) + " has no socket '" + socket + "'; its sockets: " + joined(sockets) + ".");
                }

                nlohmann::json warnings = nlohmann::json::array();
                if (parent.name.empty())
                {
                    warnings.push_back("The parent has no name: after a prefab / JSON round trip (e.g. an undone "
                                       "entity_delete) the link is re-resolved by name and cannot find it.");
                }
                else
                {
                    events::scene::FindEntitiesByNameQuery byName;
                    byName.name = parent.name;
                    const std::size_t namesakes = dispatcher.query(byName).size();
                    if (namesakes > 1)
                    {
                        warnings.push_back(std::to_string(namesakes) + " entities are named '" + parent.name +
                                           "': after a prefab / JSON round trip (e.g. an undone entity_delete) the link "
                                           "is re-resolved by that name and may bind to another of them. Give the parent "
                                           "a unique name (entity_rename).");
                    }
                }

                const std::optional<AttachmentData> previous = rig::socketAttachment(child.handle);
                const SocketState before = captureState(child.handle, previous);
                const bool changed = !(before.kind == SocketState::Kind::Attached && before.parent == toId(parent.handle) &&
                                       before.socket == socket && before.active == active);
                if (changed)
                {
                    if (const std::optional<std::string> warning = unrestorableLinkWarning(previous))
                    {
                        warnings.push_back(*warning);
                    }

                    events::socket::AttachToSocketCommand attach;
                    attach.childEntity = child.handle;
                    attach.parentEntity = parent.handle;
                    attach.socketName = socket;
                    if (!dispatcher.execute(attach))
                    {
                        return ToolResult::error("AttachToSocket failed for " + entityLabel(child) + " (logs_read has the "
                                                 "reason)");
                    }
                    if (!active)
                    {
                        events::socket::SetSocketActiveCommand off;
                        off.entity = child.handle;
                        off.active = false;
                        dispatcher.execute(off);
                    }

                    SocketState after;
                    after.kind = SocketState::Kind::Attached;
                    after.parent = toId(parent.handle);
                    after.socket = socket;
                    after.active = active;
                    after.transform = rig::localTransform(child.handle);

                    const std::string description =
                        "MCP: Attach to socket (" + child.name + " -> " + parent.name + "." + socket + ")";
                    undo::UndoRecorder recorder(description);
                    recorder.add(std::make_unique<undo::SocketAttachmentUndo>(toId(child.handle), description, before,
                                                                              std::move(after)));
                    recorder.push();
                }

                events::socket::GetSocketWorldTransformQuery worldQuery;
                worldQuery.parentEntity = parent.handle;
                worldQuery.socketName = socket;
                nlohmann::json out{
                    {"child", entityId(child.handle)},
                    {"parent", entityId(parent.handle)},
                    {"socket", socket},
                    {"active", active},
                    {"changed", changed},
                    {"previous", attachmentJson(previous)},
                    {"socketWorld", socketWorldJson(dispatcher.query(worldQuery))},
                    {"warnings", warnings}
                };
                std::string summary = (changed ? "Attached " : "Already attached: ") + entityLabel(child) + " to socket '" +
                    socket + "' of " + entityLabel(parent) + (active ? "." : " (inactive).");
                summary += warningCount(warnings);
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        void registerDetachFromSocket(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_detach_from_socket";
            tool.title = "Detach from socket";
            tool.description =
                "Detach an entity from its socket (removes its SocketAttachment component). keepWorldPose=true "
                "(default) leaves it where it is in the world; false keeps the transform the engine leaves behind (the "
                "local position then holds the last socket WORLD position, which is wrong under a moved hierarchy "
                "parent). One undo step (re-attaches it and restores its transform). Returns {detached, transform "
                "(local, after), warnings}.";
            tool.inputSchema = closedObject({
                {"child", schema::entity("The attached entity")},
                {"keepWorldPose", schema::boolean("Keep the world pose it had on the socket. Default true.")}
            }, {"child"});
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"child", "keepWorldPose"}, "animation_detach_from_socket");
                const ArgReader reader(args);
                const services::EntityData child = requireEntity(reader, "child");
                const bool keepWorldPose = reader.optBool("keepWorldPose", true);

                const std::optional<AttachmentData> previous = rig::socketAttachment(child.handle);
                if (!isLinked(previous))
                {
                    throw std::runtime_error(entityLabel(child) + (previous.has_value()
                        ? " has an empty SocketAttachment component: it is not attached to a socket."
                        : " is not attached to a socket."));
                }
                const SocketState before = captureState(child.handle, previous);
                nlohmann::json warnings = nlohmann::json::array();
                if (const std::optional<std::string> warning = unrestorableLinkWarning(previous))
                {
                    warnings.push_back(*warning);
                }

                auto& dispatcher = events::EventDispatcher::instance();
                std::optional<services::TransformData> world;
                if (keepWorldPose)
                {
                    events::scene::GetWorldTransformQuery worldQuery;
                    worldQuery.entity = child.handle;
                    world = dispatcher.query(worldQuery);
                    if (!world.has_value())
                    {
                        warnings.push_back("The world pose could not be read: the transform is left as the engine "
                                           "leaves it.");
                    }
                }

                events::socket::DetachFromSocketCommand detach;
                detach.childEntity = child.handle;
                dispatcher.execute(detach);
                if (world.has_value())
                {
                    // The updater wrote the socket's WORLD position into the local position; convert back.
                    events::scene::SetWorldTransformCommand restore;
                    restore.entity = child.handle;
                    restore.worldTransform = *world;
                    dispatcher.execute(restore);
                }

                SocketState after;
                after.transform = rig::localTransform(child.handle);
                const nlohmann::json transform =
                    after.transform.has_value() ? transformToJson(*after.transform) : nlohmann::json(nullptr);

                const std::string description = "MCP: Detach from socket (" + child.name + ")";
                undo::UndoRecorder recorder(description);
                recorder.add(std::make_unique<undo::SocketAttachmentUndo>(toId(child.handle), description, before,
                                                                          std::move(after)));
                recorder.push();

                nlohmann::json out{
                    {"child", entityId(child.handle)},
                    {"detached", attachmentJson(previous)},
                    {"keepWorldPose", keepWorldPose},
                    {"transform", transform},
                    {"warnings", warnings}
                };
                const std::string summary = "Detached " + entityLabel(child) + " from socket '" + previous->socketName +
                    "'." + warningCount(warnings);
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        void registerSetSocketActive(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_set_socket_active";
            tool.title = "Set socket attachment active";
            tool.description =
                "Switch an entity's socket attachment on or off without detaching it: while off, the entity stays "
                "where it is and no longer follows the socket. One undo step. Returns {changed, attachment}.";
            tool.inputSchema = closedObject({
                {"child", schema::entity("The attached entity")},
                {"active", schema::boolean("true = follow the socket")}
            }, {"child", "active"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"child", "active"}, "animation_set_socket_active");
                const ArgReader reader(args);
                const services::EntityData child = requireEntity(reader, "child");
                const bool active = reader.requireBool("active");

                const std::optional<AttachmentData> link = rig::socketAttachment(child.handle);
                if (!isLinked(link))
                {
                    throw std::runtime_error(entityLabel(child) + " is not attached to a socket.");
                }
                const bool before = link->isActive;
                const bool changed = before != active;
                if (changed)
                {
                    const std::optional<services::TransformData> transform = rig::localTransform(child.handle);
                    events::socket::SetSocketActiveCommand command;
                    command.entity = child.handle;
                    command.active = active;
                    events::EventDispatcher::instance().execute(command);

                    undo::UndoRecorder recorder(std::string(active ? "MCP: Activate" : "MCP: Deactivate") +
                                                " socket attachment (" + child.name + ")");
                    recorder.add(std::make_unique<undo::SocketActiveUndo>(toId(child.handle), child.name, before, active,
                                                                          transform));
                    recorder.push();
                }

                nlohmann::json out{
                    {"child", entityId(child.handle)},
                    {"changed", changed},
                    {"attachment", attachmentJson(rig::socketAttachment(child.handle))}
                };
                const std::string summary = (changed ? std::string(active ? "Activated" : "Deactivated")
                                                     : std::string("Unchanged:")) +
                    " the socket attachment of " + entityLabel(child) + ".";
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }
    }

    void registerAnimationSocketTools(ToolRegistry& registry, const ToolContext&)
    {
        registerListSockets(registry);
        registerAttachToSocket(registry);
        registerDetachFromSocket(registry);
        registerSetSocketActive(registry);
    }
}
