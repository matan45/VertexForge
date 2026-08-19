// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "EntityAPI.hpp"
#include "NativeHelpers.hpp"
#include "../scripting/NativeAPIRegistry.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/project/SceneEvents.hpp"
#include "../../../services/events/scene/ScenePersistenceEvents.hpp"
#include "../../../services/events/project/ProjectEvents.hpp"
#include "components/Components.hpp"
#include <filesystem>

namespace core::api
{
    namespace
    {
        // Resolve a script-provided prefab path relative to the project working
        // directory (same convention as SceneAPI::resolveScenePath).
        std::string resolvePrefabPath(const std::string& path)
        {
            std::filesystem::path p(path);
            if (p.is_absolute())
            {
                return path;
            }

            auto& dispatcher = events::EventDispatcher::instance();
            auto projectPathOpt = dispatcher.query(events::project::GetProjectPathQuery{});
            if (projectPathOpt.has_value())
            {
                std::filesystem::path projectFile(projectPathOpt.value());
                std::filesystem::path projectDir = projectFile.parent_path();
                return (projectDir / p).lexically_normal().string();
            }

            return path;
        }

        value::Value getTransformVec3(events::EventDispatcher& dispatcher,
                                      std::span<const value::Value> args,
                                      glm::vec3(*accessor)(const services::TransformData&),
                                      const glm::vec3& defaultVal)
        {
            if (args.empty()) return makeVec3Array(defaultVal);
            int64_t id = extractInt64(args[0]);
            if (id < 0) return makeVec3Array(defaultVal);

            events::scene::GetTransformQuery query;
            query.entity = intToEntity(id);
            auto result = dispatcher.query(query);
            if (result.has_value())
            {
                return makeVec3Array(accessor(*result));
            }
            return makeVec3Array(defaultVal);
        }

        value::Value setTransformVec3(events::EventDispatcher& dispatcher,
                                      std::span<const value::Value> args,
                                      void(*mutator)(services::TransformData&, float, float, float),
                                      const char* context)
        {
            if (args.size() < 4)
            {
                vfLogError("[Script] Entity.{}: missing arguments (expected entity id, x, y, z)", context);
                return value::Value(std::monostate{});
            }
            int64_t id = extractInt64(args[0], context);
            if (id < 0) return value::Value(std::monostate{});

            events::scene::GetTransformQuery getQuery;
            getQuery.entity = intToEntity(id);
            auto currentTransform = dispatcher.query(getQuery);
            if (!currentTransform.has_value())
            {
                vfLogError("[Script] Entity.{}: entity {} has no transform", context, id);
                return value::Value(std::monostate{});
            }

            services::TransformData newTransform = *currentTransform;
            mutator(newTransform, extractFloat(args[1], context),
                    extractFloat(args[2], context), extractFloat(args[3], context));

            events::scene::SetTransformCommand cmd;
            cmd.entity = intToEntity(id);
            cmd.transform = newTransform;
            dispatcher.execute(cmd);
            return value::Value(std::monostate{});
        }

        void registerLifecycleFunctions(services::ScriptInterpreter* interpreter,
                                        events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_entity_getSelf",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                    return value::Value(entityToInt(NativeAPIRegistry::getCurrentEntity()));
                }});

            interpreter->registerNativeFunction("_native_entity_findByName",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty())
                    {
                        vfLogError("[Script] Entity.findByName: missing name argument");
                        return value::Value(static_cast<int64_t>(-1));
                    }
                    std::string name = extractString(args[0], "Entity.findByName");
                    if (name.empty()) return value::Value(static_cast<int64_t>(-1));

                    events::scene::FindEntitiesByNameQuery query;
                    query.name = name;
                    auto results = dispatcher.query(query);
                    if (!results.empty())
                    {
                        return value::Value(entityToInt(results[0]));
                    }
                    return value::Value(static_cast<int64_t>(-1));
                }});

            interpreter->registerNativeFunction("_native_entity_findAll",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty())
                    {
                        return value::Value(std::make_shared<value::NativeArray>(
                            0, value::ValueType::INT));
                    }
                    std::string name = extractString(args[0]);
                    if (name.empty())
                    {
                        return value::Value(std::make_shared<value::NativeArray>(
                            0, value::ValueType::INT));
                    }

                    events::scene::FindEntitiesByNameQuery query;
                    query.name = name;
                    auto results = dispatcher.query(query);

                    auto arr = std::make_shared<value::NativeArray>(
                        results.size(), value::ValueType::INT);
                    for (size_t i = 0; i < results.size(); ++i)
                    {
                        arr->set(i, value::Value(entityToInt(results[i])));
                    }
                    return value::Value(arr);
                }});

            interpreter->registerNativeFunction("_native_entity_findWithComponent",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty())
                    {
                        vfLogError("[Script] Entity.findWithComponent: missing type argument");
                        return value::Value(std::make_shared<value::NativeArray>(
                            0, value::ValueType::INT));
                    }
                    std::string typeName = extractString(args[0], "Entity.findWithComponent");
                    auto compType = stringToComponentType(typeName);
                    if (!compType.has_value())
                    {
                        return value::Value(std::make_shared<value::NativeArray>(
                            0, value::ValueType::INT));
                    }

                    events::scene::GetEntitiesWithComponentQuery query;
                    query.componentType = *compType;
                    auto results = dispatcher.query(query);

                    auto arr = std::make_shared<value::NativeArray>(
                        results.size(), value::ValueType::INT);
                    for (size_t i = 0; i < results.size(); ++i)
                    {
                        arr->set(i, value::Value(entityToInt(results[i])));
                    }
                    return value::Value(arr);
                }});

            interpreter->registerNativeFunction("_native_entity_isValid",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(false);
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(false);

                    events::scene::GetEntityQuery query;
                    query.entity = intToEntity(id);
                    return value::Value(dispatcher.query(query).has_value());
                }});

            // _native_entity_getUUID(entityId) -> int
            // The persistent scene UUID, as opposed to the transient entt handle every other
            // Entity:: native takes. Needed because RegisterStreamingSourceCommand (and any
            // future owner-keyed API) keys ownership on the UUID, not the handle.
            // Returned as a bit-preserving reinterpretation into mType's int64: a UUID above
            // 2^63 reads back negative, and round-trips exactly through the
            // static_cast<uint64_t>(extractInt64(...)) on the consuming side. Treat it as an
            // opaque token - pass it through, never print or compare it. 0 means "no UUID".
            interpreter->registerNativeFunction("_native_entity_getUUID",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(static_cast<int64_t>(0));

                    auto entity = resolveEntity(args[0]);
                    if (!entity.has_value()) return value::Value(static_cast<int64_t>(0));

                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto* uuidComp = registry.try_get<components::UUIDComponent>(*entity);
                    if (!uuidComp) return value::Value(static_cast<int64_t>(0));

                    return value::Value(static_cast<int64_t>(uuidComp->id.getValue()));
                }});

            interpreter->registerNativeFunction("_native_entity_isActive",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(false);
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(false);

                    events::scene::GetEntityQuery query;
                    query.entity = intToEntity(id);
                    auto result = dispatcher.query(query);
                    return value::Value(result.has_value() && result->isActive);
                }});

            interpreter->registerNativeFunction("_native_entity_setActive",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::scene::SetEntityActiveCommand cmd;
                    cmd.entity = intToEntity(id);
                    cmd.isActive = extractBool(args[1], "Entity.setActive");
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_entity_getName",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(std::string(""));
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::string(""));

                    events::scene::GetEntityQuery query;
                    query.entity = intToEntity(id);
                    auto result = dispatcher.query(query);
                    if (result.has_value()) return value::Value(result->name);
                    return value::Value(std::string(""));
                }});

            interpreter->registerNativeFunction("_native_entity_setName",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::scene::SetEntityNameCommand cmd;
                    cmd.entity = intToEntity(id);
                    cmd.newName = extractString(args[1]);
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_entity_create",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    std::string name = "New Entity";
                    if (!args.empty())
                    {
                        name = extractString(args[0]);
                        if (name.empty()) name = "New Entity";
                    }

                    events::scene::CreateEntityCommand cmd;
                    cmd.name = name;
                    if (args.size() >= 2)
                    {
                        int64_t parentId = extractInt64(args[1]);
                        if (parentId >= 0) cmd.parent = intToEntity(parentId);
                    }
                    return value::Value(entityToInt(dispatcher.execute(cmd)));
                }});

            // _native_entity_instantiate(prefabPath [, parentId]) -> int
            // Instantiates a .vfPrefab (mesh/material/collider etc.) and returns the
            // root entity id, or -1 on failure. Path may be project-relative.
            interpreter->registerNativeFunction("_native_entity_instantiate",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty())
                    {
                        vfLogError("[Script] Entity.instantiate: missing prefab path argument");
                        return value::Value(static_cast<int64_t>(-1));
                    }
                    std::string path = extractString(args[0], "Entity.instantiate");
                    if (path.empty()) return value::Value(static_cast<int64_t>(-1));

                    events::scene::LoadPrefabCommand cmd;
                    cmd.filePath = resolvePrefabPath(path);
                    if (args.size() >= 2)
                    {
                        int64_t parentId = extractInt64(args[1]);
                        if (parentId >= 0) cmd.parent = intToEntity(parentId);
                    }
                    auto result = dispatcher.execute(cmd);
                    if (!result.has_value())
                    {
                        vfLogError("[Script] Entity.instantiate: failed to load prefab '{}'", cmd.filePath);
                        return value::Value(static_cast<int64_t>(-1));
                    }
                    return value::Value(entityToInt(*result));
                }});

            interpreter->registerNativeFunction("_native_entity_destroy",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(std::monostate{});
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(std::monostate{});

                    events::scene::DeleteEntityCommand cmd;
                    cmd.entity = intToEntity(id);
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                }});
        }

        void registerTransformFunctions(services::ScriptInterpreter* interpreter,
                                        events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_entity_getPosition",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    return getTransformVec3(dispatcher, args,
                        [](const services::TransformData& t) { return t.position; },
                        glm::vec3(0.0f));
                }});

            interpreter->registerNativeFunction("_native_entity_setPosition",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    return setTransformVec3(dispatcher, args,
                        [](services::TransformData& t, float x, float y, float z)
                        { t.position = {x, y, z}; }, "setPosition");
                }});

            interpreter->registerNativeFunction("_native_entity_getRotation",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    return getTransformVec3(dispatcher, args,
                        [](const services::TransformData& t) { return t.rotation; },
                        glm::vec3(0.0f));
                }});

            interpreter->registerNativeFunction("_native_entity_setRotation",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    return setTransformVec3(dispatcher, args,
                        [](services::TransformData& t, float x, float y, float z)
                        { t.rotation = {x, y, z}; }, "setRotation");
                }});

            interpreter->registerNativeFunction("_native_entity_getScale",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    return getTransformVec3(dispatcher, args,
                        [](const services::TransformData& t) { return t.scale; },
                        glm::vec3(1.0f));
                }});

            interpreter->registerNativeFunction("_native_entity_setScale",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    return setTransformVec3(dispatcher, args,
                        [](services::TransformData& t, float x, float y, float z)
                        { t.scale = {x, y, z}; }, "setScale");
                }});

            // World-space reads (VK-1458 OOP Transform). The local get/set
            // natives above operate on the entity's own TransformComponent;
            // these resolve the scene-graph world transform, which differs
            // for parented entities.
            interpreter->registerNativeFunction("_native_entity_getWorldPosition",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return makeVec3Array(glm::vec3(0.0f));
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return makeVec3Array(glm::vec3(0.0f));

                    events::scene::GetWorldTransformQuery query;
                    query.entity = intToEntity(id);
                    auto result = dispatcher.query(query);
                    return makeVec3Array(result.has_value() ? result->position
                                                            : glm::vec3(0.0f));
                }});

            // Euler angles in degrees, same convention as _native_entity_getRotation.
            interpreter->registerNativeFunction("_native_entity_getWorldRotation",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return makeVec3Array(glm::vec3(0.0f));
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return makeVec3Array(glm::vec3(0.0f));

                    events::scene::GetWorldTransformQuery query;
                    query.entity = intToEntity(id);
                    auto result = dispatcher.query(query);
                    return makeVec3Array(result.has_value() ? result->rotation
                                                            : glm::vec3(0.0f));
                }});
        }

        void registerParentChildFunctions(services::ScriptInterpreter* interpreter,
                                          events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_entity_setParent",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(false);
                    int64_t id = extractInt64(args[0]);
                    int64_t parentId = extractInt64(args[1]);
                    if (id < 0) return value::Value(false);

                    events::scene::ReparentEntityCommand cmd;
                    cmd.entity = intToEntity(id);
                    cmd.newParent = intToEntity(parentId);
                    return value::Value(dispatcher.execute(cmd));
                }});

            interpreter->registerNativeFunction("_native_entity_getParent",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(static_cast<int64_t>(-1));
                    int64_t id = extractInt64(args[0]);
                    if (id < 0) return value::Value(static_cast<int64_t>(-1));

                    events::scene::GetEntityQuery query;
                    query.entity = intToEntity(id);
                    auto result = dispatcher.query(query);
                    if (result.has_value() && result->parent.has_value())
                    {
                        return value::Value(entityToInt(result->parent.value()));
                    }
                    return value::Value(static_cast<int64_t>(-1));
                }});

            interpreter->registerNativeFunction("_native_entity_getChildren",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty())
                    {
                        return value::Value(std::make_shared<value::NativeArray>(
                            0, value::ValueType::INT));
                    }
                    int64_t id = extractInt64(args[0]);
                    if (id < 0)
                    {
                        return value::Value(std::make_shared<value::NativeArray>(
                            0, value::ValueType::INT));
                    }

                    events::scene::GetEntityQuery query;
                    query.entity = intToEntity(id);
                    auto result = dispatcher.query(query);
                    if (result.has_value())
                    {
                        auto arr = std::make_shared<value::NativeArray>(
                            result->children.size(), value::ValueType::INT);
                        for (size_t i = 0; i < result->children.size(); ++i)
                        {
                            arr->set(i, value::Value(entityToInt(result->children[i])));
                        }
                        return value::Value(arr);
                    }
                    return value::Value(std::make_shared<value::NativeArray>(
                        0, value::ValueType::INT));
                }});
        }
    }

    void EntityAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        registerLifecycleFunctions(interpreter, dispatcher);
        registerTransformFunctions(interpreter, dispatcher);
        registerParentChildFunctions(interpreter, dispatcher);
    }
}
