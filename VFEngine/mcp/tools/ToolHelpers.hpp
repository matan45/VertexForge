#pragma once

#include "../protocol/ArgReader.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Entity / transform helpers shared by the tool groups and by mcp/undo.
// Main thread only: every function that dispatches assumes it runs inside a
// Main-affinity handler or a runOnMain task.
namespace mcp::tools
{
    inline services::EntityHandle toHandle(uint32_t id)
    {
        services::EntityHandle handle;
        handle.id = static_cast<uint64_t>(id);
        return handle;
    }

    inline uint32_t toId(const services::EntityHandle& handle)
    {
        return static_cast<uint32_t>(handle.id);
    }

    inline nlohmann::json entityId(const services::EntityHandle& handle)
    {
        return handle.isValid() ? nlohmann::json(toId(handle)) : nlohmann::json(nullptr);
    }

    inline nlohmann::json entityId(const std::optional<services::EntityHandle>& handle)
    {
        return handle.has_value() ? entityId(*handle) : nlohmann::json(nullptr);
    }

    inline nlohmann::json transformToJson(const services::TransformData& transform)
    {
        return {
            {"position", vec3ToJson(transform.position)},
            {"rotation", vec3ToJson(transform.rotation)},
            {"scale", vec3ToJson(transform.scale)}
        };
    }

    inline std::optional<services::EntityData> findEntity(const services::EntityHandle& entity)
    {
        events::scene::GetEntityQuery query;
        query.entity = entity;
        return events::EventDispatcher::instance().query(query);
    }

    // Every entity argument is checked against the live registry first so the
    // model gets "does not exist" instead of a silent no-op from the handler.
    inline services::EntityData requireEntity(const ArgReader& reader, const char* name)
    {
        uint32_t id = reader.requireEntity(name);
        auto data = findEntity(toHandle(id));
        if (!data.has_value())
        {
            throw std::runtime_error("Entity " + std::to_string(id) + " (argument '" + name +
                                     "') does not exist; use entity_find or scene_get_hierarchy");
        }
        return *data;
    }

    inline services::EntityHandle sceneRoot()
    {
        return events::EventDispatcher::instance().query(events::scene::GetRootEntityQuery{});
    }

    inline void rejectRoot(const services::EntityData& entity, const char* action)
    {
        if (entity.handle == sceneRoot())
        {
            throw std::runtime_error(std::string("Cannot ") + action + " the scene root entity");
        }
    }

    inline nlohmann::json entitySummary(const services::EntityData& entity)
    {
        nlohmann::json children = nlohmann::json::array();
        for (const services::EntityHandle& child : entity.children)
        {
            children.push_back(entityId(child));
        }
        return {
            {"id", entityId(entity.handle)},
            {"name", entity.name},
            {"active", entity.isActive},
            {"effectivelyActive", entity.isEffectivelyActive},
            {"parent", entityId(entity.parent)},
            {"children", std::move(children)},
            {"transform", transformToJson(entity.localTransform)},
            {"worldTransform", transformToJson(entity.worldTransform)}
        };
    }

    // Index of `child` among `parent`'s children; nullopt when the parent is gone or
    // the child is not under it.
    inline std::optional<int> childIndex(const services::EntityHandle& parent, const services::EntityHandle& child)
    {
        auto data = findEntity(parent);
        if (!data.has_value())
        {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < data->children.size(); ++i)
        {
            if (data->children[i] == child)
            {
                return static_cast<int>(i);
            }
        }
        return std::nullopt;
    }

    struct TransformEdit
    {
        services::TransformData before;
        services::TransformData after;
    };

    // Overlays the provided position / rotationEuler / scale onto the current
    // local transform and writes it back. Returns the transform before and after.
    inline TransformEdit applyTransform(const ArgReader& reader, const services::EntityHandle& entity)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::GetTransformQuery query;
        query.entity = entity;
        auto current = dispatcher.query(query);
        if (!current.has_value())
        {
            throw std::runtime_error("Entity " + std::to_string(toId(entity)) + " has no transform");
        }

        TransformEdit edit{*current, *current};
        if (auto position = reader.optVec3("position"))
        {
            edit.after.position = *position;
        }
        if (auto rotation = reader.optVec3("rotationEuler"))
        {
            edit.after.rotation = *rotation;  // TransformData stores Euler degrees
        }
        if (auto scale = reader.optVec3("scale"))
        {
            edit.after.scale = *scale;
        }

        events::scene::SetTransformCommand command;
        command.entity = entity;
        command.transform = edit.after;
        dispatcher.execute(command);
        return edit;
    }

    inline bool hasTransformArgs(const ArgReader& reader)
    {
        return reader.has("position") || reader.has("rotationEuler") || reader.has("scale");
    }

    inline nlohmann::json transformProperties()
    {
        return {
            {"position", schema::vec3("Local position [x,y,z] relative to the parent")},
            {"rotationEuler", schema::vec3("Local rotation as Euler angles in DEGREES [pitch(x), yaw(y), roll(z)]")},
            {"scale", schema::vec3("Local scale [x,y,z]")}
        };
    }

    // ------------------------------------------------------------------
    // Curated (built-in) component bindings - defined in ComponentTools.cpp so
    // the generic tools and mcp/undo reuse the same JSON mapping.
    // ------------------------------------------------------------------

    struct BuiltinComponentInfo
    {
        std::string name;
        std::string fieldHelp;
    };

    std::vector<BuiltinComponentInfo> builtinComponentTypes();

    // All of these throw std::runtime_error for an unknown type name. get/patch
    // return nullopt when the entity lacks the component; patch throws ArgError for
    // an unknown or mistyped field and std::runtime_error when the engine rejects it.
    bool addBuiltinComponent(const std::string& type, const services::EntityHandle& entity);
    bool removeBuiltinComponent(const std::string& type, const services::EntityHandle& entity);
    std::optional<nlohmann::json> getBuiltinComponent(const std::string& type, const services::EntityHandle& entity);
    std::optional<nlohmann::json> patchBuiltinComponent(const std::string& type, const services::EntityHandle& entity,
                                                        const nlohmann::json& patch);
}
