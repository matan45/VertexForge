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
                "IBL, GI, fog, weather), character animation (animators, clips, sockets, IK), visual effects "
                "(effects, per-entity parameters, the edit preview), water (the ocean, lakes and pools, the "
                "water look) and game UI (canvases, widgets, HUD placement, UI prefabs) are edited.";
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

                // VK-1657: an effect is an asset (.vfVFX) the VFX component plays, tuned per entity.
                text += "## Visual effects\n\n"
                        "An effect is a .vfVFX file: ONE emitter and its node graph (spawn rate, lifetime, size, colour, "
                        "blend mode, the over-lifetime modifiers, forces, events); there is no separate graph asset. A combo "
                        "(.vfVFXSequence) times several effects, sounds and script cues on one timeline. A typical pass: "
                        "assets_rescan (once, after copying effects in) -> vfx_list_assets {contains:\"fire\"} -> "
                        "entity_create -> vfx_add_emitter {entity, effect} -> vfx_play {entity} -> wait about the particle "
                        "lifetime -> viewport_screenshot -> vfx_set_params {entity, params:{size:1.6, color:[1, 0.6, 0.25]}} "
                        "-> viewport_screenshot.\n\n"
                        "- Project assets only: the tools list and attach effects inside the project, the files an exported "
                        "game carries. The engine ships ready-made effects and combos in its assets/vfx/library folder: copy "
                        "that folder into the project as assets/vfx/library (the combos reference that path), then call "
                        "assets_rescan so the asset database registers the copied files.\n"
                        "- VFX component: vfx_add_emitter adds one (component_add / component_get / component_set / "
                        "component_remove {type:\"VFX\"} work too, fields above). It plays its effect at the entity's world "
                        "transform: autoPlay starts it when Play mode starts, loop repeats it.\n"
                        "- Per-entity parameters: spawnRate, lifetime, size, color, blendMode and sortOrder (vfx_set_params) "
                        "and the other overridable emitter parameters in otherParams (component_set) are saved with the "
                        "scene, prefab or world sector and apply in the edit preview, in Play mode and in exported games. The "
                        ".vfVFX is never written: null (or vfx_set_params reset) returns a parameter to its authored value, "
                        "and vfx_set_params reports each one's override, authored and effective value.\n"
                        "- Colour: color is a TINT multiplied over every authored colour, [1, 1, 1, 1] = as authored. An "
                        "effect with a colour-over-lifetime gradient (most library effects, fire_loop among them) replaces "
                        "its particles' start colour every frame, so changing the start colour would not show; the tint "
                        "recolours the start colour and the gradient alike. birthColor reports the colour particles are born "
                        "with, authored and effective.\n"
                        "- Blend modes: alpha (alpha-over), additive (glow: adds light), premultiplied (the fire-into-smoke "
                        "gradient trick), multiply (darkens what is behind). sortOrder (-256 to 256) is the draw order among "
                        "the effects of the same pass: higher draws later, on top.\n"
                        "- Edit preview: vfx_play plays a component in edit mode with its parameters. It follows the entity's "
                        "transform and every component change (a new value applies in place; removing a parameter, or "
                        "changing the effect or loop, restarts it) and ends with vfx_stop, play_start, a scene load or clear, "
                        "deleting the entity or removing the component. A new preview starts empty: particles need about "
                        "their lifetime (vfx_play reports it) to fill in before viewport_screenshot shows the effect.\n"
                        "- Play mode: every VFX component spawns its effect with its parameters when Play starts and plays it "
                        "when autoPlay is on (otherwise a script calls VFX::play). Scripts (vf://docs/mtype-api, section 9) "
                        "spawn effects with VFX::spawnAt / VFX::spawnAtLooping, change a spawned instance with VFX::setOverride "
                        "/ VFX::setOverrideTint, and react through IVFXEventListener (particle events) and "
                        "IVFXComboCueListener (combo cues); everything a script spawns is destroyed when Play stops.\n"
                        "- Combos: vfx_list_assets lists them with their steps and cues, but they cannot be attached to an "
                        "entity in this version; a script spawns one with VFX::spawnCombo.\n"
                        "- Undo: each vfx_add_emitter and vfx_set_params is one undo step (none when nothing changes); vfx_play, "
                        "vfx_stop and assets_rescan are not undoable. vfx_add_emitter, vfx_set_params and vfx_play refuse in "
                        "Play mode and while a scene load is pending; vfx_list_assets and vfx_stop work in any mode.\n\n";

                // VK-1658: water is the ocean entity (one per scene) plus any number of water bodies.
                text += "## Water\n\n"
                        "A scene has at most one ocean plus any number of lakes and pools (water bodies). Tools: water_get "
                        "(all of it, read-only), water_create_body / water_set_body (lakes and pools), water_set_params (the "
                        "look of all water), ocean_enable / ocean_set_params (the sea). A typical pass: terrain_create + "
                        "terrain_sculpt (a basin and a low edge) -> ocean_enable {enabled:true, levelAboveTerrain:2} -> "
                        "water_create_body {center:[x, z], halfExtents:[hx, hz], levelAboveTerrain:-0.3, "
                        "terrainReference:\"rim\"} -> ocean_set_params {seaState:5, windDirection:200} -> water_set_params "
                        "{color:{shallowColor:[0.05, 0.45, 0.5], opacity:0.8}, shoreline:{shoreWavesEnabled:true}} -> "
                        "viewport_screenshot.\n\n"
                        "- Ocean: an endless sea at one level (it follows the camera) with FFT waves in three bands (swell, "
                        "agitation, ripples). ocean_set_params sets the level, the sea state (seaState = Beaufort 0-12, which "
                        "derives every band, or windSpeed), windDirection, choppiness, single bands (bands.swell / agitation / "
                        "ripples), weatherDriven / weatherResponse and physics. While it is weather-driven the weather owns the "
                        "wind and wave bands and the sea state (it drives them whenever the weather is enabled), so changing "
                        "them is refused: pass weatherDriven:false in the same call to author them. The ocean ignores its "
                        "transform: set its level with ocean_set_params {level}.\n"
                        "- Hidden vs removed: ocean_enable {enabled:false} HIDES the ocean and keeps every setting. A hidden "
                        "ocean shows no sea, runs no waves, floats nothing and has no underwater effect, but still holds the "
                        "look of every lake and pool; ocean_enable {enabled:true} shows it again. {enabled:false, remove:true} "
                        "deletes the ocean entity, and the lakes fall back to the default look.\n"
                        "- Lakes and pools: axis-aligned rectangles (center [x, z] and halfExtents [hx, hz] in metres, no "
                        "rotation or scale) at their own level, with a depth (their floor). A body is placed by its LOCAL "
                        "position, so keep it at the scene root. Inside its rectangle a body replaces the sea (only the 8 "
                        "nearest the camera hide the sea beneath them). waves picks the FFT bands a body shows (default none = "
                        "flat); they move only while the ocean is visible. There are no rivers: a long, narrow lake stands in "
                        "for one. Remove a body with entity_delete.\n"
                        "- Levels: level is in world metres; levelAboveTerrain is metres above the terrain: under the body's "
                        "rectangle at its terrainReference (lowest, center or rim), and for the ocean at its terrainPoint "
                        "[x, z] or the terrain's lowest point. water_create_body warns when the water would show past the rim, "
                        "lie under the terrain everywhere or reach off the terrain.\n"
                        "- Look (water_set_params): one look for all water. color, refraction, shoreFoam, reflections, "
                        "absorption (only while refraction is above 0) and interactiveRipples apply to every lake, pool and "
                        "the ocean; shoreline (shoaling, breaking waves and the wet sand on the terrain), caustics and "
                        "antiTiling need the visible ocean, and shoreline needs a terrain. A scene with lakes but no ocean gets "
                        "a hidden ocean on its first water_set_params to hold the look.\n"
                        "- Physics: drag and buoyancyStrength (ocean_set_params physics) tune floating in ALL water; "
                        "physicsEnabled switches the sea's own buoyancy, and a body's physicsEnabled its own (off = it floats "
                        "nothing). Only dynamic rigid bodies float, in Play mode.\n"
                        "- entity_set_active does not hide water: use water_set_body {active:false} or ocean_enable "
                        "{enabled:false}. The generic entity tools refuse to delete, duplicate, move or deactivate the ocean.\n"
                        "- Strict updates: unknown keys or out-of-range values fail the whole call and change nothing. "
                        "water_get lists every key (ranges:true adds the valid ranges); its ocean params (for "
                        "ocean_set_params), look (for water_set_params) and each body (for water_set_body) can be passed back "
                        "as they are (changed:false).\n"
                        "- Persistence: scene_save saves the ocean, visible or hidden, and every body with the scene.\n"
                        "- Undo: each water_create_body, water_set_body, water_set_params, ocean_enable and ocean_set_params "
                        "is one undo step (none when nothing changes). They refuse in Play mode and while a scene load is "
                        "pending, and water_create_body also in World mode; water_get works in any mode. Scripts use the Ocean "
                        "class and IWaterListener (vf://docs/mtype-api, section 10).\n\n";

                // VK-1659: game UI is a canvas entity with widget entities under it, one section per UI component.
                text += "## UI\n\n"
                        "Game UI (a HUD, a menu) is a canvas entity with widget entities under it. Tools: ui_create_canvas, "
                        "ui_add_widget, ui_set / ui_get, ui_list_widget_types (every widget type, its sections, keys and "
                        "valid ranges), ui_save_prefab / ui_instantiate_prefab and ui_click. A typical HUD pass: "
                        "ui_create_canvas {name:\"HUD\"} -> ui_add_widget {type:\"ProgressBar\", parent:<canvas>, "
                        "name:\"HealthBar\", placement:{preset:\"topLeft\", size:[320, 28], margin:24}} -> ui_add_widget "
                        "{type:\"Label\", parent:<canvas>, name:\"ScoreLabel\", text:\"Score: 0\", placement:{preset:"
                        "\"topRight\", size:[300, 48], margin:[24, 20]}, label:{horizontalAlignment:\"right\"}} -> "
                        "ui_add_widget {type:\"Button\", parent:<canvas>, name:\"PauseButton\", text:\"Pause\", placement:"
                        "{preset:\"top\", size:[160, 56], margin:[0, 20]}} -> a script (vf://docs/mtype-api, section 11) "
                        "-> script_attach -> play_start -> viewport_screenshot {settleFrames:5} -> ui_click {entity:"
                        "<PauseButton>} -> play_stop.\n\n"
                        "- Canvas: referenceResolution (default 1920 x 1080) is the screen size the layout is authored "
                        "for. scaleMode scaleWithScreenSize (the default) scales every size, margin and offset by "
                        "min(width / 1920, height / 1080) on other screens; constantPixelSize keeps them in pixels. There is "
                        "one render mode: in Play mode and in an exported game the canvas draws over the scene. In Edit "
                        "mode the editor shows it only as a flat preview quad in the scene (referenceResolution / "
                        "pixelsPerUnit metres, at the canvas entity) with its textured images and its labels: colour-only "
                        "panels, buttons and progress bars appear only in Play. Check UI with play_start + "
                        "viewport_screenshot {settleFrames:5} (fonts load in the background). The editor lays the HUD out "
                        "for its window size. sortOrder orders canvases (higher on top), but labels always draw over images.\n"
                        "- Placement: placement {preset, size [w, h], margin, offset [dx, dy], relativeTo} places a widget. "
                        "Presets: topLeft, top, topRight, left, center, right, bottomLeft, bottom, bottomRight, stretchTop, "
                        "stretchMiddle, stretchBottom, stretchLeft, stretchCenter, stretchRight, stretch. Sizes, margins (n, "
                        "[x, y] or {left, right, top, bottom}, inward) and offsets (+y down) are canvas pixels. relativeTo "
                        "\"parent\" (the default) places a widget inside its parent widget and keeps it there on every "
                        "screen; \"canvas\" places it on the whole canvas.\n"
                        "- How the engine positions: every element is anchored to the WHOLE canvas (anchors are canvas "
                        "fractions, y up; pivot.y counts from the top), never to its parent, so moving a parent does not "
                        "move its children. ui_set moves them along (moveDescendants, default true; positions only, sizes "
                        "stay). The rect section (anchorMin, anchorMax, pivot, sizeDelta, anchoredPosition) is there for "
                        "fine control; ui_get reports each element's resolved rect in canvas pixels (x, y from the "
                        "top-left).\n"
                        "- Layout groups (a LayoutGroup, a ListView) place their DIRECT children every frame in Play mode "
                        "(vertical, horizontal or grid; spacing and padding are pixels, not scaled), overriding the "
                        "children's placement. A child's own children do not follow, so keep a laid-out child's caption on "
                        "the child itself.\n"
                        "- Widgets (ui_add_widget type): Rect (an invisible container), Image (a dark panel, or a .vfImage), "
                        "Label (text; fontRef is a .vfFont, \"\" = the default font), Button (a coloured panel with its own "
                        "label), Checkbox (a box plus a label part to its right; clicking the label toggles it too), Slider, "
                        "ProgressBar (draws its own track and fill), Dropdown (options plus its own label), TextInput, "
                        "ListView (rows from a prefab) and LayoutGroup. text sets the visible caption: a Label's text, the "
                        "Button or Checkbox label, the Dropdown or TextInput placeholder. Remove a widget with entity_delete.\n"
                        "- State colours: a Button, Checkbox, TextInput or Dropdown repaints its image every frame from its "
                        "state colours, so recolour a button with button.normalColor (and hoveredColor, pressedColor), not "
                        "image.colorTint.\n"
                        "- ListView: its rows are instances of an item template (listView.itemTemplateRef, a .vfPrefab), "
                        "listView.itemCount of them, placed by its layout group. Build one row (best a single entity: a Label, "
                        "or an Image with a label section), ui_save_prefab it, entity_delete it, then set the template. Rows "
                        "are rebuilt from the template and never saved; scripts use UI::setListItemCount and "
                        "UI::getListItem.\n"
                        "- Strict updates: ui_set changes only the keys it is given, section by section (canvas, rect, image, "
                        "label, button, checkbox, slider, progressBar, dropdown, textInput, layoutGroup, listView, mask, "
                        "scroll); unknown keys, wrong types and out-of-range values fail the whole call and change nothing. "
                        "ui_set (and ui_add_widget) adds an image, label, layoutGroup, mask or scroll section to a widget that "
                        "lacks one. A ui_get report can be passed back as is (changed:false): read-only keys (runtime state "
                        "such as button.currentState, the canvas theme, type, children) are accepted while they keep their "
                        "value; resolved, validation and ranges are ignored.\n"
                        "- Themes: a canvas theme (.vfTheme) and a widget's style key are reported read-only. Under a themed "
                        "canvas a styled widget gets its theme values back on every scene load, over values set with ui_set.\n"
                        "- Prefabs and persistence: scene_save saves the UI with the scene. ui_save_prefab writes a widget "
                        "or canvas subtree to a .vfPrefab inside the project (and links the entity to it); "
                        "ui_instantiate_prefab puts a widget prefab back under a canvas or a widget, and a canvas prefab "
                        "anywhere outside other UI.\n"
                        "- ui_click {entity} (Play mode only) presses a Button (pressed, released, clicked) or toggles a "
                        "Checkbox (radio groups included) the way a mouse click does, after the same checks (active, "
                        "interactable, inside a canvas, not under a modal window), so listener scripts run. Scripts start on "
                        "the first Play frame. It never hovers.\n"
                        "- Undo: each ui_create_canvas, ui_add_widget, ui_set and ui_instantiate_prefab is one undo step "
                        "(none when nothing changes); ui_save_prefab and ui_click are not undoable. The editing tools refuse "
                        "in Play mode and while a scene load is pending, and ui_create_canvas, ui_add_widget and "
                        "ui_instantiate_prefab also in World mode (sector streaming would unload the UI); ui_get and "
                        "ui_list_widget_types work in any mode. The layer open in the editor's UI Layer Builder is the "
                        "builder's own working copy, not scene UI: ui_get does not list it and the tools refuse it. Scripts "
                        "drive the UI with the UI class and the listener interfaces such as IUIButtonListener "
                        "(vf://docs/mtype-api, section 11).\n\n";

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
