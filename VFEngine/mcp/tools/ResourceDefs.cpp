#include "CoreTools.hpp"
#include "ContentHelpers.hpp"
#include "MTypeApiDoc.hpp"
#include "PathSandbox.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr const char* scriptsPrefix = "vf://scripts/";
        constexpr const char* mtypeApiUri = "vf://docs/mtype-api";
        constexpr const char* mtypeModulePrefix = "vf://docs/mtype-api/";
        constexpr std::size_t logLines = 500;
        constexpr std::size_t maxListedScripts = 200;

        // Worker thread: hop to the main thread for the project query.
        fs::path scriptsRoot(const ToolContext& context)
        {
            nlohmann::json root = context.runOnMain([]() -> nlohmann::json
            {
                return pathToUtf8(scriptsRootOnMain());
            });
            return pathFromUtf8(root.get<std::string>());
        }

        // RFC 3986 path encoding: unreserved characters and '/' stay, every other
        // byte becomes %XX (the template reader percent-decodes it back).
        std::string percentEncodePath(const std::string& path)
        {
            static constexpr char hex[] = "0123456789ABCDEF";
            std::string out;
            for (char c : path)
            {
                const unsigned char byte = static_cast<unsigned char>(c);
                const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                    (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_' || byte == '~' ||
                    byte == '/';
                if (unreserved)
                {
                    out += c;
                }
                else
                {
                    out += '%';
                    out += hex[byte >> 4];
                    out += hex[byte & 0x0F];
                }
            }
            return out;
        }

        // vf://scripts/{+path} only takes scripts-root-relative paths; resolveInside
        // then rejects anything that escapes the root ("..", symlinks).
        void rejectAbsoluteScriptPath(const std::string& path)
        {
            if (path.empty())
            {
                throw ArgError("vf://scripts/ needs a script path, e.g. vf://scripts/game/Player.mt");
            }
            const fs::path requested = pathFromUtf8(path);
            if (path.front() == '/' || path.front() == '\\' || requested.is_absolute() || requested.has_root_name())
            {
                throw ArgError("script path '" + path + "' must be relative to the scripts root, e.g. game/Player.mt");
            }
        }

        void addSceneHierarchy(ResourceRegistry& registry)
        {
            ResourceDef resource;
            resource.uri = "vf://scene/hierarchy";
            resource.name = "scene_hierarchy";
            resource.title = "Scene hierarchy";
            resource.description =
                "The open scene's entity tree, same JSON as scene_get_hierarchy with no depth limit: "
                "{root, entityCount, returned, entities:[{id, name, active, components, position, rotation, "
                "scale, children}]}. Transforms are local; rotations are Euler degrees. A terrain reports "
                "terrainTileCount instead of listing its tile entities.";
            resource.mimeType = "application/json";
            resource.affinity = ThreadAffinity::Main;
            resource.reader = []() -> ResourceContents
            {
                return {"vf://scene/hierarchy", "application/json", buildSceneHierarchy(-1, true).dump(2)};
            };
            registry.add(std::move(resource));
        }

        void addLogs(ResourceRegistry& registry)
        {
            ResourceDef resource;
            resource.uri = "vf://logs";
            resource.name = "logs";
            resource.title = "Editor log";
            resource.description =
                "The last 500 editor console lines (engine + script output), oldest first, one per line as "
                "'[seq][level] message'. Script runtime errors carry a '[Script]' prefix (compile errors come "
                "back from scripts_build). For filtering or incremental polling use the logs_read tool.";
            resource.mimeType = "text/plain";
            // Only touches the mutex-guarded console buffer, so it never needs the main thread.
            resource.affinity = ThreadAffinity::Worker;
            resource.reader = []() -> ResourceContents
            {
                const LogSelection selection = collectLogs(std::nullopt, util::LogLevel::Trace, logLines);
                std::string text;
                for (const util::LogEntry& entry : selection.entries)
                {
                    text += "[" + std::to_string(entry.sequenceNumber) + "][" + logLevelName(entry.level) + "] " +
                            entry.message + "\n";
                }
                return {"vf://logs", "text/plain", std::move(text)};
            };
            registry.add(std::move(resource));
        }

        void addScripts(ResourceRegistry& registry, const ToolContext& context)
        {
            ResourceTemplateDef scriptTemplate;
            scriptTemplate.uriTemplate = "vf://scripts/{+path}";
            scriptTemplate.prefix = scriptsPrefix;
            scriptTemplate.name = "script";
            scriptTemplate.title = "mType script";
            scriptTemplate.description =
                "Source of an mType script. 'path' is relative to the project scripts root: game/** (the game "
                "scripts, listed by resources/list) or lib/** (the read-only engine API, e.g. "
                "vf://scripts/lib/engine/Physics.mt).";
            scriptTemplate.mimeType = "text/x-mtype";
            scriptTemplate.affinity = ThreadAffinity::Worker;
            scriptTemplate.reader = [context](const std::string& uri, const std::string& path) -> ResourceContents
            {
                rejectAbsoluteScriptPath(path);
                const fs::path root = scriptsRoot(context);
                ScriptReadResult script = readScript(root, path);
                switch (script.status)
                {
                case ScriptReadStatus::NotFound:
                    throw ResourceNotFound("no script at '" + path + "' (resources/list shows the game scripts)");
                case ScriptReadStatus::Unreadable:
                    throw std::runtime_error("Script is unreadable or larger than 1 MB: " + path);
                case ScriptReadStatus::OpenFailed:
                    throw std::runtime_error("Failed to open script: " + path);
                case ScriptReadStatus::Ok:
                    break;
                }
                return {uri, "text/x-mtype", std::move(script.source)};
            };
            registry.addTemplate(std::move(scriptTemplate));

            ResourceListContributor contributor;
            contributor.affinity = ThreadAffinity::Worker;
            contributor.list = [context]() -> std::vector<ResourceDef>
            {
                std::vector<ResourceDef> listed;
                for (const std::string& path : listGameScripts(scriptsRoot(context), maxListedScripts))
                {
                    ResourceDef resource;
                    resource.uri = scriptsPrefix + percentEncodePath(path);
                    resource.name = path;
                    resource.description = "mType game script";
                    resource.mimeType = "text/x-mtype";
                    listed.push_back(std::move(resource));
                }
                return listed;
            };
            registry.addContributor(std::move(contributor));
        }

        std::string pluginFieldLine(const nlohmann::json& field)
        {
            if (!field.is_object())
            {
                return {};
            }
            std::string line = "- `" + field.value("name", std::string("?")) + "`: " +
                               field.value("type", std::string("?"));
            if (field.contains("min") || field.contains("max"))
            {
                line += " [" + (field.contains("min") ? field["min"].dump() : std::string()) + ".." +
                        (field.contains("max") ? field["max"].dump() : std::string()) + "]";
            }
            if (field.value("readOnly", false))
            {
                line += " (read-only)";
            }
            if (field.value("hidden", false))
            {
                line += " (hidden)";
            }
            return line + "\n";
        }

        void addComponentDocs(ResourceRegistry& registry)
        {
            ResourceDef resource;
            resource.uri = "vf://docs/components";
            resource.name = "components";
            resource.title = "Component reference";
            resource.description =
                "Every component type the agent can edit: the built-in ones (component_add / component_get / "
                "component_set / component_remove) with their fields, and the reflected plugin components "
                "(component_*_generic) loaded in this editor. Also how terrain, the lighting environment (sky, "
                "IBL, GI, fog, weather) and character animation (animators, clips, sockets, IK) are edited.";
            resource.mimeType = "text/markdown";
            resource.affinity = ThreadAffinity::Main;
            resource.reader = []() -> ResourceContents
            {
                std::string text =
                    "# Components\n\n"
                    "## Built-in\n\n"
                    "Use the name as the 'type' of component_add / component_get / component_set / "
                    "component_remove. Vectors are [x,y,z]; asset fields take a project-relative path.\n\n";
                for (const BuiltinComponentInfo& info : builtinComponentTypes())
                {
                    text += "### " + info.name + "\n\n" + info.fieldHelp + "\n\n";
                }

                // VK-1653: Terrain / TerrainTile show up in scene_get_hierarchy but are owned by the terrain
                // service, not editable component data.
                text += "## Terrain\n\n"
                        "Terrain is not a component_add type: use the terrain_* tools (terrain_create, "
                        "terrain_generate_heightmap, terrain_sculpt, terrain_add_layer, terrain_paint_layer, "
                        "terrain_height_at, terrain_save, terrain_delete; terrain_get_info describes it). The generic "
                        "entity tools refuse terrain and terrain tile entities.\n\n";

                // VK-1654: the lighting environment is render-settings data, not components.
                text += "## Environment & lighting\n\n"
                        "Tools: environment_get / environment_set (procedural sky, clouds, time of day, sun), ibl_set / "
                        "ibl_bake (HDR or dynamic-sky image-based lighting, reflection probes), gi_get / gi_set / "
                        "gi_debug (global illumination), fog_get / fog_set (volumetric distance and height fog), "
                        "weather_get / weather_set. A typical pass: environment_set {atmosphere:{enabled:true}, "
                        "timeOfDay:17.85} -> fog_set {enabled:true, heightFogDensity:0.08, anisotropy:0.8} -> ibl_set "
                        "{source:'dynamicSky'} (or {source:'hdr', path, showAsSky:false} to keep the procedural sky) "
                        "-> gi_set {preset:'High'} -> viewport_screenshot after each step.\n\n"
                        "- Saved vs live: every setting exists twice, the scene's saved value and the live value the "
                        "renderer uses. The *_set tools write both; the *_get tools report both. While weather is "
                        "enabled it overrides some live values (fog density and colour; cloud coverage, density, type, "
                        "wind, tint and ambient; the sky's sun irradiance, aerial and ambient intensity); the saved "
                        "values apply again when weather is off (sun irradiance and aerial intensity after the next "
                        "scene load).\n"
                        "- Strict updates: unknown keys, wrong types and out-of-range values fail the whole call and "
                        "change nothing; the *_get tools list every key with its valid range. A *_get object can be "
                        "passed back as is: read-only keys are accepted while they keep their current value.\n"
                        "- Sky and ambient: the visible sky is the procedural atmosphere while atmosphere.enabled, "
                        "otherwise the HDR skybox (ibl_set source hdr). Ambient light and reflections come from the "
                        "sky capture while the atmosphere is enabled with dynamicAmbient on (ibl_set source "
                        "dynamicSky), otherwise from the HDR. The scene's directional light is the sun: environment_set "
                        "rotates it with timeOfDay or the sun angles, and creates a 'Sun' light only when the atmosphere "
                        "ends up enabled, createSunIfMissing is true (the default) and the scene has no directional "
                        "light.\n"
                        "- Fog: uniformDensity is homogeneous (distance) fog within maxDistance; height fog adds "
                        "heightFogDensity near the ground (heightFogOffset, heightFogFalloff). Light shafts appear where "
                        "geometry shadows the sun's light inside the fog (they need the fog enabled and a directional "
                        "light that casts shadows): raise scatteringCoefficient for stronger shafts; anisotropy 0.6-0.9 "
                        "makes them brightest when looking toward the sun. Fog renders only while enabled.\n"
                        "- Persistence: scene_save saves the atmosphere, clouds, fog and GI (the scene's .vfSettings) "
                        "and the IBL (on the scene root). Weather is session-only: it is not saved with the scene. "
                        "gi_debug views are not saved either.\n"
                        "- Undo: each environment_set, ibl_set, gi_set, fog_set and weather_set is one undo step. The "
                        "*_set tools refuse in Play mode (play_stop first); the *_get tools, gi_debug and ibl_bake "
                        "also work in Play mode.\n\n";

                // VK-1655: the scene's look (post-processing, shadows, anti-aliasing) is render-settings data too.
                text += "## Post-processing & render settings\n\n"
                        "Tools: render_settings_get / render_settings_set (tone mapping and exposure, auto exposure, "
                        "bloom, color grading, SSAO, vignette, chromatic aberration, film grain, depth of field, edge "
                        "detection, the DLSS upscaler, MSAA and the shadow quality), postprocess_effect_list / "
                        "postprocess_effect_set (post-process effects registered by plugins). A typical pass: "
                        "render_settings_set {postProcess:{enabled:true, toneMapping:{enabled:true, mode:'AgX', "
                        "exposure:1.2}, bloom:{enabled:true, intensity:1.0}}} -> viewport_screenshot.\n\n"
                        "- Master switch: no built-in effect renders unless postProcess.enabled is true, tone mapping "
                        "included; exposure and the tone curve also need toneMapping.enabled. While autoExposure.enabled, "
                        "change autoExposure.exposureCompensation instead of toneMapping.exposure.\n"
                        "- Anti-aliasing: display.msaa (Off, X2, X4, X8) or the upscaler: postProcess.upscale "
                        "{enabled:true, quality:'Native'} is DLAA (full resolution), the other qualities render fewer "
                        "pixels. DLSS needs an NVIDIA RTX GPU, and MSAA is ignored while the upscaler runs. There is no "
                        "FXAA.\n"
                        "- Effect order: SSAO, depth of field and auto exposure work on the HDR scene; tone mapping; "
                        "then color grading, bloom, edge detection, chromatic aberration, vignette and film grain. "
                        "Plugin effects run before (stage beforeTonemap) or after (afterTonemap) the tone mapping.\n"
                        "- Strict updates, saved vs live and render_settings_get objects passed back as is work as "
                        "for the environment tools. Color grading LUTs are .cube files relative to the project; \"\" "
                        "clears one. The volumetric fog belongs to fog_set.\n"
                        "- Persistence: scene_save saves every render_settings_set value with the scene (its "
                        ".vfSettings); in an exported game a player's graphics preset overrides the shadows and MSAA. "
                        "Plugin effect toggles are per session, like the plugin's own: not saved with the scene.\n"
                        "- Undo: each render_settings_set and postprocess_effect_set is one undo step; both refuse in "
                        "Play mode.\n\n";

                // VK-1656: an animator is an asset (.vfAnimator) the Animator component points at.
                text += "## Animation\n\n"
                        "A skinned Mesh entity animates through an animator (.vfAnimator): parameters, states that play "
                        "a clip (.vfAnim) or a 1D / 2D blend tree, and transitions whose conditions test the parameters. "
                        "A typical pass: assets_import the model (an FBX / GLB with animations also writes one "
                        "<model>_<take>.vfAnim per take) -> entity_create + component_add {type:\"Mesh\"} -> "
                        "animation_list_clips {entity} -> component_add {type:\"Animator\", data:{defaultClip}} -> "
                        "animation_set_state_machine {entity, parameters:[{name:\"speed\", type:\"Float\"}], states:"
                        "[{name:\"Idle\", clip}, {name:\"Run\", clip}], transitions:[{from:\"Idle\", to:\"Run\", conditions:"
                        "[{parameter:\"speed\", op:\">\", value:0.1}]}, {from:\"Run\", to:\"Idle\", conditions:"
                        "[{parameter:\"speed\", op:\"<=\", value:0.1}]}]} -> a script sets \"speed\" -> play_start -> "
                        "animation_get_runtime_state.\n\n"
                        "- Animator component: component_add {type:\"Animator\", data:{defaultClip}} creates a one-state "
                        "animator next to the mesh and assigns it; data:{animatorRef} assigns an existing one. Writable "
                        "fields: animatorRef, retargetRef, applyRootMotion; the others are read-only facts from the file. "
                        "component_remove clears the assignment (the file stays).\n"
                        "- Clips: animation_list_clips lists the clips that fit an entity's skeleton, natively (bone "
                        "names) or through its retargetRef (humanoid roles), with their coverage.\n"
                        "- State machine: animation_get_state_machine reads it; animation_set_state_machine edits "
                        "parameters, states, transitions and the default state by name (assignTo assigns the animator to "
                        "more entities). Validation errors block the save; warnings are returned. Live animators reload "
                        "after every save.\n"
                        "- Preview and debugging: animation_preview plays a state or any clip on an entity in edit mode "
                        "(then viewport_screenshot); animation_get_runtime_state reads the current state, parameter "
                        "values and transition conditions, also in Play mode.\n"
                        "- Scripts drive the animator with Animator::setFloat / setBool / setTrigger (vf://docs/mtype-api); "
                        "the names and types must match the animator's parameters.\n"
                        "- Sockets: animation_list_sockets, animation_attach_to_socket, animation_detach_from_socket and "
                        "animation_set_socket_active attach entities (a weapon, a hat) to the sockets of a mesh.\n"
                        "- IK: animation_ik_get, animation_ik_set and animation_ik_remove edit an entity's IK chains; "
                        "animation_ik_set_target moves a chain's runtime target (world space, not saved). IK runs only on "
                        "entities with an Animator.\n"
                        "- Undo: component_add / component_set / component_remove of the Animator, "
                        "animation_set_state_machine (the file's previous content, unless the file changed since), "
                        "animation_ik_set, animation_ik_remove and the socket attach / detach / active changes are undo "
                        "steps; creating a .vfAnimator, animation_preview and animation_ik_set_target are not. "
                        "animation_set_state_machine, animation_preview, animation_ik_set, animation_ik_remove and "
                        "creating a .vfAnimator refuse in Play mode and while a scene load is pending.\n\n";

                text += "## Plugin components\n\n"
                        "Edit with component_add_generic / component_get_generic / component_set_generic / "
                        "component_remove_generic. Field values use the scene-file JSON format.\n\n";
                nlohmann::json plugin;
                try
                {
                    plugin = pluginComponentTypes();
                }
                catch (const std::exception&)
                {
                    plugin = nullptr;  // no plugin component handler in this editor
                }
                if (!plugin.is_array())
                {
                    text += "_Plugin component types are unavailable._\n";
                }
                else if (plugin.empty())
                {
                    text += "_No plugin components are registered._\n";
                }
                else
                {
                    for (const nlohmann::json& type : plugin)
                    {
                        if (!type.is_object())
                        {
                            continue;
                        }
                        text += "### " + type.value("name", std::string("?"));
                        const std::string owner = type.value("plugin", std::string());
                        text += owner.empty() ? "\n\n" : " (plugin " + owner + ")\n\n";
                        const auto fields = type.find("fields");
                        if (fields != type.end() && fields->is_array())
                        {
                            for (const nlohmann::json& field : *fields)
                            {
                                text += pluginFieldLine(field);
                            }
                        }
                        text += "\n";
                    }
                }
                return {"vf://docs/components", "text/markdown", std::move(text)};
            };
            registry.add(std::move(resource));
        }

        void addMTypeDocs(ResourceRegistry& registry, const ToolContext& context)
        {
            ResourceDef resource;
            resource.uri = mtypeApiUri;
            resource.name = "mtype_api";
            resource.title = "mType scripting API";
            resource.description =
                "READ BEFORE WRITING ANY SCRIPT. The mType language rules that make scripts_build pass "
                "(imports, @Script/Behaviour structure, typed declarations, '::' statics, reserved words), "
                "a verified WASD movement example, the signatures of the core engine API parsed from this "
                "project's scripts/lib, and a catalogue of every other lib module.";
            resource.mimeType = "text/markdown";
            resource.affinity = ThreadAffinity::Worker;
            resource.timeout = std::chrono::milliseconds(30000);
            resource.reader = [context]() -> ResourceContents
            {
                std::optional<fs::path> root;
                try
                {
                    root = scriptsRoot(context);
                }
                catch (const std::exception&)
                {
                    // No project loaded (or the main thread is busy): the primer alone still helps.
                }
                std::string text = root.has_value()
                    ? mtypedoc::buildMTypeApiDoc(*root)
                    : mtypedoc::primer() + "\n# API signatures\n\nsignatures unavailable: no project is loaded\n";
                return {mtypeApiUri, "text/markdown", std::move(text)};
            };
            registry.add(std::move(resource));

            ResourceTemplateDef moduleTemplate;
            moduleTemplate.uriTemplate = "vf://docs/mtype-api/{+module}";
            moduleTemplate.prefix = mtypeModulePrefix;
            moduleTemplate.name = "mtype_module";
            moduleTemplate.title = "mType module signatures";
            moduleTemplate.description =
                "Public signatures of one engine scripting module, by its path under scripts/lib without "
                "'.mt', e.g. vf://docs/mtype-api/engine/Physics or vf://docs/mtype-api/math/Quaternion. "
                "vf://docs/mtype-api lists every module.";
            moduleTemplate.mimeType = "text/markdown";
            moduleTemplate.affinity = ThreadAffinity::Worker;
            moduleTemplate.reader = [context](const std::string& uri, const std::string& module) -> ResourceContents
            {
                if (!mtypedoc::isValidModuleId(module) &&
                    !(module.ends_with(".mt") && mtypedoc::isValidModuleId(module.substr(0, module.size() - 3))))
                {
                    // Before any main-thread hop: a malformed id never touches the project.
                    throw ArgError("invalid mType module '" + module + "': use an id such as 'engine/Physics'");
                }
                return {uri, "text/markdown", mtypedoc::buildModuleDoc(scriptsRoot(context), module)};
            };
            registry.addTemplate(std::move(moduleTemplate));
        }
    }

    void registerCoreResources(ResourceRegistry& registry, MainThreadQueue& queue)
    {
        const ToolContext context{queue};
        addSceneHierarchy(registry);
        addLogs(registry);
        addScripts(registry, context);
        addComponentDocs(registry);
        addMTypeDocs(registry, context);
    }
}
