#include "CoreTools.hpp"
#include "../protocol/ArgReader.hpp"
#include "ContentHelpers.hpp"
#include "PathSandbox.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/render/MaterialEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace mcp::tools
{
    namespace
    {
        nlohmann::json entityId(const services::EntityHandle& handle)
        {
            return handle.isValid() ? nlohmann::json(static_cast<uint32_t>(handle.id)) : nlohmann::json(nullptr);
        }

        const char* componentTypeName(services::ComponentTypeId type)
        {
            using services::ComponentTypeId;
            switch (type)
            {
            case ComponentTypeId::None: return "None";
            case ComponentTypeId::Transform: return "Transform";
            case ComponentTypeId::Camera: return "Camera";
            case ComponentTypeId::Name: return "Name";
            case ComponentTypeId::Parent: return "Parent";
            case ComponentTypeId::Children: return "Children";
            case ComponentTypeId::WorldTransform: return "WorldTransform";
            case ComponentTypeId::IBL: return "IBL";
            case ComponentTypeId::Mesh: return "Mesh";
            case ComponentTypeId::DirectionalLight: return "DirectionalLight";
            case ComponentTypeId::PointLight: return "PointLight";
            case ComponentTypeId::SpotLight: return "SpotLight";
            case ComponentTypeId::Material: return "Material";
            case ComponentTypeId::Billboard: return "Billboard";
            case ComponentTypeId::AudioSource2D: return "AudioSource2D";
            case ComponentTypeId::AudioSource3D: return "AudioSource3D";
            case ComponentTypeId::Script: return "Script";
            case ComponentTypeId::Collider: return "Collider";
            case ComponentTypeId::RigidBody: return "RigidBody";
            case ComponentTypeId::Vehicle: return "Vehicle";
            case ComponentTypeId::PhysicsAnimation: return "PhysicsAnimation";
            case ComponentTypeId::Animator: return "Animator";
            case ComponentTypeId::VFX: return "VFX";
            case ComponentTypeId::Text: return "Text";
            case ComponentTypeId::UICanvas: return "UICanvas";
            case ComponentTypeId::UIRect: return "UIRect";
            case ComponentTypeId::UIImage: return "UIImage";
            case ComponentTypeId::UIScroll: return "UIScroll";
            case ComponentTypeId::UILayoutGroup: return "UILayoutGroup";
            case ComponentTypeId::UILabel: return "UILabel";
            case ComponentTypeId::UIButton: return "UIButton";
            case ComponentTypeId::UITextInput: return "UITextInput";
            case ComponentTypeId::UICheckbox: return "UICheckbox";
            case ComponentTypeId::UIDropdown: return "UIDropdown";
            case ComponentTypeId::UITabs: return "UITabs";
            case ComponentTypeId::UISlider: return "UISlider";
            case ComponentTypeId::UIProgressBar: return "UIProgressBar";
            case ComponentTypeId::SocketAttachment: return "SocketAttachment";
            case ComponentTypeId::SocketOverride: return "SocketOverride";
            case ComponentTypeId::NavmeshAgent: return "NavmeshAgent";
            case ComponentTypeId::RenderTexture: return "RenderTexture";
            case ComponentTypeId::Controller: return "Controller";
            case ComponentTypeId::Decal: return "Decal";
            case ComponentTypeId::UIStyle: return "UIStyle";
            case ComponentTypeId::UITooltip: return "UITooltip";
            case ComponentTypeId::UIWindow: return "UIWindow";
            case ComponentTypeId::UIListView: return "UIListView";
            case ComponentTypeId::Terrain: return "Terrain";
            case ComponentTypeId::TerrainTile: return "TerrainTile";
            }
            return "Unknown";
        }

        // Bookkeeping components every entity carries; listing them is noise.
        bool isStructuralComponent(services::ComponentTypeId type)
        {
            using services::ComponentTypeId;
            return type == ComponentTypeId::Transform || type == ComponentTypeId::Name ||
                type == ComponentTypeId::Parent || type == ComponentTypeId::Children ||
                type == ComponentTypeId::WorldTransform;
        }

        // EntityQueryService::getComponentTypes does not report lights or materials,
        // so ask their services directly. A missing handler means "not present".
        template <typename THasQuery>
        bool hasComponent(const services::EntityHandle& handle)
        {
            try
            {
                THasQuery query;
                query.entity = handle;
                return events::EventDispatcher::instance().query(query);
            }
            catch (const std::runtime_error&)
            {
                return false;
            }
        }

        nlohmann::json componentNames(const services::EntityData& entity)
        {
            nlohmann::json names = nlohmann::json::array();
            for (services::ComponentTypeId type : entity.components)
            {
                if (!isStructuralComponent(type))
                {
                    names.push_back(componentTypeName(type));
                }
            }

            auto appendIfMissing = [&](services::ComponentTypeId type, bool present)
            {
                if (present && !entity.hasComponent(type))
                {
                    names.push_back(componentTypeName(type));
                }
            };
            appendIfMissing(services::ComponentTypeId::DirectionalLight,
                            hasComponent<events::scene::HasDirectionalLightComponentQuery>(entity.handle));
            appendIfMissing(services::ComponentTypeId::PointLight,
                            hasComponent<events::scene::HasPointLightComponentQuery>(entity.handle));
            appendIfMissing(services::ComponentTypeId::SpotLight,
                            hasComponent<events::scene::HasSpotLightComponentQuery>(entity.handle));
            appendIfMissing(services::ComponentTypeId::Material,
                            hasComponent<events::material::HasMaterialComponentQuery>(entity.handle));
            return names;
        }

        bool isPlayMode()
        {
            return events::EventDispatcher::instance().query(events::editor::IsPlayModeQuery{});
        }

        // Relative paths resolve against the project's working directory (the same
        // base the editor uses for the startup scene); absolute paths pass through.
        // `path` is the agent's UTF-8; engine-side strings (workingDirectory,
        // command file paths) stay narrow like the rest of the engine.
        std::filesystem::path resolveScenePath(const std::string& path)
        {
            std::filesystem::path scenePath = pathFromUtf8(path);
            if (scenePath.is_relative())
            {
                auto project = events::EventDispatcher::instance().query(events::project::GetCurrentProjectQuery{});
                if (!project.has_value() || project->workingDirectory.empty())
                {
                    // Never fall back to the process CWD (the editor's bin folder).
                    throw ArgError("no project is loaded, so '" + path + "' cannot be resolved; pass an absolute path");
                }
                scenePath = std::filesystem::path(project->workingDirectory) / scenePath;
            }
            return scenePath.lexically_normal();
        }

        struct HierarchyBuilder
        {
            std::unordered_map<uint64_t, const services::EntityData*> byId;
            std::unordered_set<uint64_t> visited;
            int maxDepth = -1;
            bool includeTransforms = true;
            std::size_t emitted = 0;

            nlohmann::json node(const services::EntityData& entity, int depth)
            {
                visited.insert(entity.handle.id);
                ++emitted;

                nlohmann::json out{
                    {"id", entityId(entity.handle)},
                    {"name", entity.name},
                    {"active", entity.isActive},
                    {"components", componentNames(entity)}
                };
                if (entity.isActive && !entity.isEffectivelyActive)
                {
                    out["effectivelyActive"] = false;  // an inactive ancestor hides it
                }
                if (includeTransforms)
                {
                    out["position"] = vec3ToJson(entity.localTransform.position);
                    out["rotation"] = vec3ToJson(entity.localTransform.rotation);
                    out["scale"] = vec3ToJson(entity.localTransform.scale);
                }

                if (maxDepth >= 0 && depth >= maxDepth)
                {
                    if (!entity.children.empty())
                    {
                        out["childCount"] = entity.children.size();
                    }
                    return out;
                }

                nlohmann::json children = nlohmann::json::array();
                for (const services::EntityHandle& child : entity.children)
                {
                    auto it = byId.find(child.id);
                    if (it == byId.end() || visited.contains(child.id))
                    {
                        continue;
                    }
                    children.push_back(node(*it->second, depth + 1));
                }
                if (!children.empty())
                {
                    out["children"] = std::move(children);
                }
                return out;
            }
        };

        void registerSceneNew(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "scene_new";
            tool.title = "New scene";
            tool.description =
                "Clear the current scene and start an empty one (default physics/audio/render settings). "
                "Unsaved changes are lost - call scene_save first if needed. Not allowed in Play mode.";
            tool.inputSchema = schema::object(nlohmann::json::object());
            tool.destructive = true;
            tool.handler = [](const nlohmann::json&) -> ToolResult
            {
                if (isPlayMode())
                {
                    return ToolResult::error("Cannot create a new scene in Play mode; call play_stop first");
                }
                if (!events::EventDispatcher::instance().execute(events::scene::NewSceneCommand{}))
                {
                    return ToolResult::error("NewScene failed (no scene graph)");
                }
                return ToolResult::ok({{"created", true}});
            };
            registry.add(std::move(tool));
        }

        void registerSceneSave(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "scene_save";
            tool.title = "Save scene";
            tool.description =
                "Save the current scene to a .vfScene file. 'path' is absolute or relative to the project's "
                "working directory; '.vfScene' is appended when there is no extension. Without 'path' the scene "
                "is saved over the file it was loaded from (error if it was never loaded from a file). Not allowed in Play mode. Saving "
                "to a new path does not change the scene's current path - pass 'path' again on later saves, "
                "or scene_load the file to make it current.";
            tool.inputSchema = schema::object({
                {"path", schema::string("Target .vfScene path (absolute or project-relative). Optional.")}
            });
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                auto& dispatcher = events::EventDispatcher::instance();

                if (isPlayMode())
                {
                    // Would write the running game state over the authored scene.
                    return ToolResult::error("Cannot save the scene in Play mode; call play_stop first");
                }

                std::filesystem::path target;
                if (reader.has("path"))
                {
                    std::string path = reader.requireString("path");
                    if (path.empty())
                    {
                        throw ArgError("argument 'path' must not be empty");
                    }
                    target = resolveScenePath(path);
                    if (!target.has_extension())
                    {
                        target += ".vfScene";
                    }
                }
                else
                {
                    std::string current = dispatcher.query(events::scene::GetCurrentScenePathQuery{});
                    if (current.empty())
                    {
                        return ToolResult::error("The scene has no file yet; pass 'path' to choose where to save it");
                    }
                    target = std::filesystem::path(current);
                }

                if (target.has_parent_path())
                {
                    std::error_code ec;
                    std::filesystem::create_directories(target.parent_path(), ec);
                    if (ec)
                    {
                        return ToolResult::error("Cannot create directory '" + pathToUtf8(target.parent_path()) +
                                                 "': " + ec.message());
                    }
                }

                events::scene::SaveSceneCommand command;
                command.filePath = target.string();
                if (!dispatcher.execute(command))
                {
                    return ToolResult::error("Failed to save scene to '" + pathToUtf8(target) + "' (see logs_read)");
                }
                return ToolResult::ok({{"saved", true}, {"path", pathToUtf8(target)}});
            };
            registry.add(std::move(tool));
        }

        void registerSceneLoad(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "scene_load";
            tool.title = "Load scene";
            tool.description =
                "Load a .vfScene file, replacing the current scene (unsaved changes are lost). 'path' is "
                "absolute or relative to the project's working directory. The load is DEFERRED: this call only "
                "queues it and returns immediately; the scene is rebuilt over the next frame(s). Afterwards call "
                "scene_get_hierarchy (or editor_get_info and check scenePath) to confirm it finished; entity ids "
                "from the previous scene are invalid. Not allowed in Play mode.";
            tool.inputSchema = schema::object({
                {"path", schema::string("The .vfScene file to load (absolute or project-relative)")}
            }, {"path"});
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                std::string path = reader.requireString("path");
                if (path.empty())
                {
                    throw ArgError("argument 'path' must not be empty");
                }
                if (isPlayMode())
                {
                    return ToolResult::error("Cannot load a scene in Play mode; call play_stop first");
                }

                std::filesystem::path target = resolveScenePath(path);
                std::error_code ec;
                if (!std::filesystem::is_regular_file(target, ec))
                {
                    return ToolResult::error("Scene file not found: " + pathToUtf8(target));
                }

                events::scene::LoadSceneCommand command;
                command.filePath = target.string();
                if (!events::EventDispatcher::instance().execute(command))
                {
                    return ToolResult::error("Scene load was rejected for '" + pathToUtf8(target) + "' (see logs_read)");
                }
                return ToolResult::ok({
                    {"queued", true},
                    {"path", pathToUtf8(target)},
                    {"note", "Load completes on a following frame; call scene_get_hierarchy to inspect the result."}
                });
            };
            registry.add(std::move(tool));
        }

        void registerSceneGetHierarchy(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "scene_get_hierarchy";
            tool.title = "Get scene hierarchy";
            tool.description =
                "Return the scene's entity tree. Each node: {id, name, active, components:[type names], "
                "position, rotation (Euler degrees), scale (all LOCAL to the parent), children:[...]}. "
                "'effectivelyActive': false marks an entity hidden by an inactive ancestor; 'childCount' "
                "replaces 'children' where maxDepth cut the tree. 'root' is the hidden scene root id "
                "(entities without a parent are its children).";
            tool.inputSchema = schema::object({
                {"maxDepth", schema::integer("Maximum depth below the top-level entities to expand (0 = top level only). Default: unlimited.")},
                {"includeTransforms", schema::boolean("Include local position/rotation/scale per entity. Default true.")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                int64_t maxDepth = reader.optInt("maxDepth", -1);
                if (maxDepth < -1 || maxDepth > 1000)
                {
                    throw ArgError("argument 'maxDepth' must be between 0 and 1000");
                }
                const bool includeTransforms = reader.optBool("includeTransforms", true);
                return ToolResult::ok(buildSceneHierarchy(static_cast<int>(maxDepth), includeTransforms));
            };
            registry.add(std::move(tool));
        }
    }

    nlohmann::json buildSceneHierarchy(int maxDepth, bool includeTransforms)
    {
        services::SceneHierarchyData hierarchy =
            events::EventDispatcher::instance().query(events::scene::GetSceneHierarchyQuery{});

        HierarchyBuilder builder;
        builder.maxDepth = maxDepth;
        builder.includeTransforms = includeTransforms;
        for (const services::EntityData& entity : hierarchy.entities)
        {
            builder.byId.emplace(entity.handle.id, &entity);
        }

        nlohmann::json entities = nlohmann::json::array();
        auto rootIt = builder.byId.find(hierarchy.root.id);
        if (rootIt != builder.byId.end())
        {
            builder.visited.insert(hierarchy.root.id);
            for (const services::EntityHandle& child : rootIt->second->children)
            {
                auto it = builder.byId.find(child.id);
                if (it != builder.byId.end() && !builder.visited.contains(child.id))
                {
                    entities.push_back(builder.node(*it->second, 0));
                }
            }
        }

        return {
            {"root", entityId(hierarchy.root)},
            {"entityCount", hierarchy.entities.empty() ? 0 : hierarchy.entities.size() - 1},
            {"returned", builder.emitted},
            {"entities", std::move(entities)}
        };
    }

    void registerSceneTools(ToolRegistry& registry, const ToolContext&)
    {
        registerSceneNew(registry);
        registerSceneSave(registry);
        registerSceneLoad(registry);
        registerSceneGetHierarchy(registry);
    }
}
