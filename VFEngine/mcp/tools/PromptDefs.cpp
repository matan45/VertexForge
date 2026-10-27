#include "CoreTools.hpp"
#include "../protocol/ArgReader.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <system_error>

namespace mcp::tools
{
    namespace
    {
        constexpr std::size_t maxNameLength = 40;

        std::string formatNumber(double value)
        {
            std::array<char, 32> buffer{};
            auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            if (ec != std::errc{})
            {
                return std::to_string(value);
            }
            std::string text(buffer.data(), ptr);
            if (text.find_first_of(".e") == std::string::npos)
            {
                text += ".0";  // mType float literals need the decimal point
            }
            return text;
        }

        // MCP prompt arguments arrive as strings; an empty string means "not given".
        std::optional<double> optNumber(const nlohmann::json& args, const char* name, double min, double max)
        {
            if (!args.is_object())
            {
                return std::nullopt;
            }
            auto it = args.find(name);
            if (it == args.end() || it->is_null())
            {
                return std::nullopt;
            }

            const std::string range = " between " + formatNumber(min) + " and " + formatNumber(max);
            double value = 0.0;
            if (it->is_number())
            {
                value = it->get<double>();
            }
            else if (it->is_string())
            {
                std::string text = it->get<std::string>();
                const auto first = text.find_first_not_of(" \t");
                if (first == std::string::npos)
                {
                    return std::nullopt;
                }
                text = text.substr(first, text.find_last_not_of(" \t") - first + 1);
                const char* end = text.data() + text.size();
                auto [ptr, ec] = std::from_chars(text.data(), end, value);
                if (ec != std::errc{} || ptr != end)
                {
                    throw ArgError(std::string("argument '") + name + "' must be a number" + range);
                }
            }
            else
            {
                throw ArgError(std::string("argument '") + name + "' must be a number" + range);
            }

            if (!std::isfinite(value) || value < min || value > max)
            {
                throw ArgError(std::string("argument '") + name + "' must be a number" + range);
            }
            return value;
        }

        // The game name as a PascalCase identifier for class and file names
        // ("my game" -> "MyGame"); `fallback` when nothing usable remains.
        std::string identifierName(const nlohmann::json& args, const char* fallback)
        {
            std::string raw;
            if (args.is_object())
            {
                auto it = args.find("name");
                if (it != args.end() && !it->is_null())
                {
                    if (!it->is_string())
                    {
                        throw ArgError("argument 'name' must be a string");
                    }
                    raw = it->get<std::string>();
                }
            }

            std::string out;
            bool upperNext = true;
            for (char c : raw)
            {
                const unsigned char byte = static_cast<unsigned char>(c);
                if (byte < 0x80 && std::isalnum(byte))
                {
                    out += upperNext ? static_cast<char>(std::toupper(byte)) : c;
                    upperNext = false;
                }
                else
                {
                    upperNext = true;
                }
                if (out.size() == maxNameLength)
                {
                    break;
                }
            }
            if (out.empty())
            {
                return fallback;
            }
            if (std::isdigit(static_cast<unsigned char>(out.front())))
            {
                out.insert(0, "Game");
            }
            return out;
        }

        nlohmann::json userMessage(std::string text)
        {
            return nlohmann::json::array({
                {{"role", "user"}, {"content", {{"type", "text"}, {"text", std::move(text)}}}}
            });
        }

        // Steps shared by both templates, from "read the docs" to the empty scene.
        std::string preamble(const std::string& game, const std::string& genre)
        {
            return
                "Build a small playable " + genre + " called '" + game + "' in the open VertexForge editor, "
                "using only the vertexforge MCP tools named below. Work step by step and check every tool "
                "result before moving on.\n\n"
                "1. Read the resource vf://docs/mtype-api completely before writing any script (language rules, "
                "the Behaviour base class, a verified WASD example and the core API signatures). Read "
                "vf://docs/components for the component fields, and vf://docs/mtype-api/engine/oop/RigidBody "
                "for the rigid-body script API.\n"
                "2. editor_get_info: confirm a project is open. If it reports Play mode, call play_stop.\n"
                "3. scene_new to start from an empty scene (save anything the user needs first).\n"
                "4. assets_list with type 'Mesh' to find mesh assets for the visuals (a cube or box mesh, and a "
                "capsule or sphere if there is one); their paths are the 'meshRef' values below. If the project "
                "has no suitable mesh, tell the user (assets_import can import one) and continue without the "
                "Mesh components.\n"
                "5. entity_create 'Sun' with rotationEuler [-50, 30, 0], then component_add type "
                "'DirectionalLight'.\n";
        }

        // Optional rigged-character step shared by both templates (VK-1656). The script side is
        // section 8 of vf://docs/mtype-api; the tools named here are the core animation tools.
        std::string animatedPlayer(const std::string& step, const std::string& script)
        {
            return
                step + ". Optional animated player, only when the user has a rigged FBX character with idle and "
                "run takes: assets_import it (the results list its .vfMesh files and one .vfAnim clip per take) "
                "and use its skinned meshes for the Player instead of the capsule, the first on 'Player' and any "
                "other on a child entity of it (entity_create with parent). On the first skinned mesh entity "
                "component_add type 'Animator' with data {\"defaultClip\": <idle .vfAnim path>} "
                "(animation_list_clips {\"entity\": <id>} lists the clips that fit its skeleton; component_get "
                "shows the created animatorRef), on the others component_add 'Animator' with data "
                "{\"animatorRef\": <that path>}. Then animation_set_state_machine on that animatorRef: a Float "
                "parameter 'speed', states 'Idle' and 'Run', Idle -> Run when speed > 0.1, Run -> Idle when speed "
                "<= 0.1. Check the run clip with animation_preview and viewport_screenshot. In " + script +
                " set 'speed' every frame (the move speed while moving, 0.0 otherwise) on the Player and on each "
                "child with an Animator, as section 8 of vf://docs/mtype-api shows (API: "
                "vf://docs/mtype-api/engine/Animator). In Play mode animation_get_runtime_state "
                "{\"entity\": <id>} should report Idle, then Run while moving.\n";
        }

        // Optional visual-effects line shared by both templates (VK-1657), before the build step so scene_save
        // keeps the VFX components. The script side is section 9 of vf://docs/mtype-api.
        std::string optionalEffects()
        {
            return
                "Optional effects, only when the user asks for them (a torch fire, sparks on impact): vfx_list_assets "
                "{\"contains\": \"fire\"} lists the project's effects (when it lists none, copy the engine's "
                "assets/vfx/library folder into the project as assets/vfx/library, then call assets_rescan); "
                "vfx_add_emitter {\"entity\": <id>, \"effect\": <path>} attaches one, vfx_set_params tunes its size and "
                "color (a tint over the authored colours), vfx_play previews it in the editor and viewport_screenshot "
                "shows it about one particle lifetime later. Section 9 of vf://docs/mtype-api shows a script that "
                "spawns an effect when its entity collides.\n";
        }

        std::string buildLoop(const std::string& game, const std::string& scenePath)
        {
            return
                "- scripts_build. If success is false, fix every '<file>: <diagnostic>' error with script_write "
                "and run scripts_build again until it succeeds. Do not attach or play before that.\n"
                "- script_attach each script to its entity (path 'game/" + game + "/<File>.mt').\n"
                "- scene_save with path '" + scenePath + "'.\n"
                "- play_start, then logs_read (minLevel 'warning') to catch script errors, then "
                "viewport_screenshot to check what the camera sees, then play_stop. Repeat the fix / build / "
                "play cycle until the game behaves, then scene_save again.\n"
                "- Finish with a short summary: entity ids, scripts written, and anything left to tune.\n";
        }

        void registerPlatformerTemplate(PromptRegistry& registry)
        {
            PromptDef prompt;
            prompt.name = "create_platformer_template";
            prompt.title = "Create a platformer template";
            prompt.description =
                "Walk the agent through building a side-on 3D platformer in the open editor: scene, ground "
                "and platforms with physics, a player with movement and jump scripts, a follow camera, then "
                "build, play-test and save.";
            prompt.arguments = {
                {"name", "Game name, used for the scene file and the script classes. Default 'Platformer'.", false},
                {"playerSpeed", "Horizontal player speed in m/s (0.5-50). Default 6.", false},
                {"jumpHeight", "Jump apex height in metres (0.1-20). Default 2.", false}
            };
            prompt.build = [](const nlohmann::json& args) -> nlohmann::json
            {
                const std::string game = identifierName(args, "Platformer");
                const double speed = optNumber(args, "playerSpeed", 0.5, 50.0).value_or(6.0);
                const double jump = optNumber(args, "jumpHeight", 0.1, 20.0).value_or(2.0);
                const std::string controller = game + "PlayerController";
                const std::string follow = game + "CameraFollow";

                std::string text = preamble(game, "side-scrolling 3D platformer");
                text +=
                    "6. entity_create 'MainCamera' at position [0, 3, -12] with rotationEuler [0, 180, 0] so it looks along +Z at "
                    "the level, then component_add type 'Camera' with data {\"isPrimary\": true}. Confirm the "
                    "view with viewport_screenshot later.\n"
                    "7. Ground: entity_create 'Ground' at [0, -0.5, 0] with scale [40, 1, 4]; component_add "
                    "'Mesh' (data {\"meshRef\": <cube path>}), 'RigidBody' (data {\"type\": \"Static\"}) and "
                    "'Collider' (data {\"shape\": \"Box\"}). Read the component back with component_get and make "
                    "the collider size match the visible box.\n"
                    "8. Platforms: create 3-4 more Static boxes the same way ('Platform1', ...), stepping up and "
                    "to the right along +X, each reachable with a jump of " + formatNumber(jump) + " m.\n"
                    "9. Player: entity_create 'Player' at [0, 1.5, 0]; component_add 'Mesh' (capsule or cube), "
                    "'RigidBody' with data {\"type\": \"Dynamic\", \"freezeRotationX\": true, \"freezeRotationY\": "
                    "true, \"freezeRotationZ\": true} and 'Collider' with data {\"shape\": \"Capsule\"} (check its "
                    "size with component_get).\n"
                    "10. Materials: material_create {\"name\": \"" + game + "Player\", \"directory\": \"materials\"} "
                    "and {\"name\": \"" + game + "Level\", \"directory\": \"materials\"}; material_assign them to "
                    "the player and to the ground and platforms.\n"
                    "11. script_write 'game/" + game + "/" + controller + ".mt': an @Script class " + controller +
                    " extends Behaviour. In onUpdate read the rigid body (this.gameObject().rigidBody(); import "
                    "lib/engine/oop/RigidBody.mt with the right number of ../ for the game/" + game + "/ folder), "
                    "keep its vertical velocity and set the horizontal X velocity to +/-" + formatNumber(speed) +
                    " from Key::A / Key::D (Key::LEFT / Key::RIGHT too). On Key::SPACE, when the vertical velocity "
                    "is close to 0 (standing), set the Y velocity to sqrt(2.0 * 9.81 * " + formatNumber(jump) +
                    ") for a jump of about " + formatNumber(jump) + " m. Store the speed and jump height as "
                    "private float fields.\n"
                    "12. script_write 'game/" + game + "/" + follow + ".mt': an @Script class " + follow +
                    " extends Behaviour for the camera. In onLateUpdate (no @Override: Behaviour does not declare "
                    "it) find the player with GameObject::find(\"Player\"), null-check it, and set the camera's "
                    "local position to the player's world position plus [0, 3, -12].\n";
                text += animatedPlayer("13", controller);
                text += optionalEffects();
                text += "14. Build, attach, save and play-test:\n";
                text += buildLoop(game, "scenes/" + game + ".vfScene");
                return userMessage(std::move(text));
            };
            registry.add(std::move(prompt));
        }

        void registerTopDownTemplate(PromptRegistry& registry)
        {
            PromptDef prompt;
            prompt.name = "create_top_down_template";
            prompt.title = "Create a top-down template";
            prompt.description =
                "Walk the agent through building a top-down 3D game in the open editor: a lit ground plane "
                "with obstacles, a WASD player, a camera that follows from above, then build, play-test and "
                "save.";
            prompt.arguments = {
                {"name", "Game name, used for the scene file and the script classes. Default 'TopDown'.", false},
                {"cameraHeight", "Camera height above the player in metres (2-200). Default 15.", false}
            };
            prompt.build = [](const nlohmann::json& args) -> nlohmann::json
            {
                const std::string game = identifierName(args, "TopDown");
                const double height = optNumber(args, "cameraHeight", 2.0, 200.0).value_or(15.0);
                const std::string mover = game + "PlayerMover";
                const std::string follow = game + "CameraFollow";

                std::string text = preamble(game, "top-down game");
                text +=
                    "6. entity_create 'MainCamera' at position [0, " + formatNumber(height) + ", 0] with "
                    "rotationEuler [-90, 0, 0] so it looks straight down; component_add type 'Camera' with data "
                    "{\"isPrimary\": true}. Confirm the view with viewport_screenshot later.\n"
                    "7. Ground: entity_create 'Ground' at [0, -0.5, 0] with scale [40, 1, 40]; component_add "
                    "'Mesh' (data {\"meshRef\": <cube path>}), 'RigidBody' (data {\"type\": \"Static\"}) and "
                    "'Collider' (data {\"shape\": \"Box\"}); check the collider size with component_get.\n"
                    "8. Obstacles: 4-6 Static boxes ('Obstacle1', ...) scattered on the ground the same way.\n"
                    "9. Player: entity_create 'Player' at [0, 1, 0]; component_add 'Mesh' (capsule or cube), "
                    "'RigidBody' with data {\"type\": \"Dynamic\", \"freezeRotationX\": true, "
                    "\"freezeRotationY\": true, \"freezeRotationZ\": true} and 'Collider' with data "
                    "{\"shape\": \"Capsule\"} (check its size with component_get).\n"
                    "10. Materials: material_create {\"name\": \"" + game + "Player\", \"directory\": \"materials\"} "
                    "and {\"name\": \"" + game + "Ground\", \"directory\": \"materials\"}; material_assign them to "
                    "the player and to the ground and obstacles.\n"
                    "11. script_write 'game/" + game + "/" + mover + ".mt': an @Script class " + mover +
                    " extends Behaviour that moves the player on the XZ plane with Key::W / A / S / D in WORLD "
                    "axes (+Z = W, +X = D) at a private float speed of 6.0: build the direction like the "
                    "PlayerMovement example in vf://docs/mtype-api (new class name, ../ count for the game/" +
                    game + "/ folder), then set the rigid body's velocity (this.gameObject().rigidBody(); import "
                    "lib/engine/oop/RigidBody.mt) to direction.normalize().multiply(this.speed) while keeping its "
                    "current Y velocity, with zero horizontal velocity when no key is held.\n"
                    "12. script_write 'game/" + game + "/" + follow + ".mt': an @Script class " + follow +
                    " extends Behaviour for the camera. In onLateUpdate (no @Override: Behaviour does not declare "
                    "it) find the player with GameObject::find(\"Player\"), null-check it, and set the camera's "
                    "local position to the player's world position plus [0, " + formatNumber(height) + ", 0].\n";
                text += animatedPlayer("13", mover);
                text += optionalEffects();
                text += "14. Build, attach, save and play-test:\n";
                text += buildLoop(game, "scenes/" + game + ".vfScene");
                return userMessage(std::move(text));
            };
            registry.add(std::move(prompt));
        }

        // VK-1659: the HUD the UI tools were built for, wired to the primer's HudController example (section 11 of
        // vf://docs/mtype-api). The placements are the ones the UI docs work through.
        void registerHudTemplate(PromptRegistry& registry)
        {
            PromptDef prompt;
            prompt.name = "create_hud";
            prompt.title = "Create a HUD";
            prompt.description =
                "Walk the agent through building an in-game HUD in the open scene: a canvas with a health bar "
                "top-left, a score label top-right and a pause button, a script that updates them and pauses the "
                "game, then a play-test with a simulated click, an undo check and a save.";
            prompt.arguments = {
                {"name", "Game name, used for the script class and folder. Default 'Game'.", false}
            };
            prompt.build = [](const nlohmann::json& args) -> nlohmann::json
            {
                const std::string game = identifierName(args, "Game");
                const std::string hud = game + "HudController";
                const std::string scriptPath = "game/" + game + "/" + hud + ".mt";

                std::string text =
                    "Build an in-game HUD for '" + game + "' in the open VertexForge editor: a health bar top-left, a "
                    "score label top-right and a pause button, wired to a script. Use only the vertexforge MCP tools "
                    "named below. Work step by step and check every tool result before moving on.\n\n"
                    "1. Read the UI section of vf://docs/components and section 11 (UI) of vf://docs/mtype-api before "
                    "writing anything: every widget is anchored to the whole canvas, scripts find widgets by their unique "
                    "names, a button click reaches every listener script, and Edit mode shows the canvas only as a flat "
                    "preview.\n"
                    "2. editor_get_info: confirm a project is open. If it reports Play mode, call play_stop. Build in the "
                    "open scene (the game the HUD belongs to). If a UI tool refuses because the scene is a World-mode "
                    "scene, tell the user and stop.\n"
                    "3. ui_list_widget_types: note the ProgressBar, Label and Button sections and the placement presets.\n"
                    "4. ui_create_canvas {\"name\": \"HUD\"}: a canvas authored for 1920 x 1080 that scales with the "
                    "screen.\n"
                    "5. ui_add_widget {\"type\": \"ProgressBar\", \"parent\": <canvas id>, \"name\": \"HealthBar\", "
                    "\"placement\": {\"preset\": \"topLeft\", \"size\": [320, 28], \"margin\": 24}}.\n"
                    "6. ui_add_widget {\"type\": \"Label\", \"parent\": <canvas id>, \"name\": \"ScoreLabel\", \"text\": "
                    "\"Score: 0\", \"placement\": {\"preset\": \"topRight\", \"size\": [300, 48], \"margin\": [24, 20]}, "
                    "\"label\": {\"horizontalAlignment\": \"right\"}}.\n"
                    "7. ui_add_widget {\"type\": \"Button\", \"parent\": <canvas id>, \"name\": \"PauseButton\", \"text\": "
                    "\"Pause\", \"placement\": {\"preset\": \"top\", \"size\": [160, 56], \"margin\": [0, 20]}}.\n"
                    "8. ui_get {\"entity\": <canvas id>, \"recursive\": true}: at the 1920 x 1080 reference size the "
                    "resolved rects are HealthBar (24, 24, 320, 28), ScoreLabel (1596, 20, 300, 48) and PauseButton "
                    "(880, 20, 160, 56). Fix any difference with ui_set.\n"
                    "9. Undo check, while still in Edit mode: ui_set {\"entity\": <ScoreLabel id>, \"label\": {\"color\": "
                    "[1, 0.8, 0.25, 1]}}, then undo, and confirm with ui_get that the label colour is white again.\n"
                    "10. script_write '" + scriptPath + "': the HudController example of section 11 of vf://docs/mtype-api "
                    "with the class renamed to " + hud + ". game/" + game + "/ is as deep as game/examples/, so the "
                    "imports stay as they are.\n"
                    "11. scripts_build. If success is false, fix every '<file>: <diagnostic>' error with script_write and "
                    "run scripts_build again until it succeeds.\n"
                    "12. script_attach {\"entity\": <canvas id>, \"path\": \"" + scriptPath + "\"}, then scene_save "
                    "(pass a path such as 'scenes/" + game + ".vfScene' when the scene was never saved).\n"
                    "13. play_start, then viewport_screenshot {\"settleFrames\": 5}: the bar top-left, the score top-right, "
                    "the button top-centre. ui_get the HealthBar twice, a moment apart: its progressBar value falls while "
                    "the game runs.\n"
                    "14. ui_click {\"entity\": <PauseButton id>}, then logs_read (minLevel 'info') for 'pause toggled', "
                    "ui_get the PauseButton (its label text is now 'Resume') and viewport_screenshot. ui_click it again to "
                    "resume, then play_stop.\n"
                    "15. Finish with a short summary: the script and anything left to tune. play_stop reloads the edit "
                    "scene, so the entity ids change: before editing the HUD again, wait until ui_get works, then find the "
                    "new ids with ui_get {} and ui_get {\"entity\": <canvas id>, \"recursive\": true}.\n";
                return userMessage(std::move(text));
            };
            registry.add(std::move(prompt));
        }
    }

    void registerCorePrompts(PromptRegistry& registry)
    {
        registerPlatformerTemplate(registry);
        registerTopDownTemplate(registry);
        registerHudTemplate(registry);
    }
}
