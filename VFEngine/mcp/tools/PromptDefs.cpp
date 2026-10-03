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
                    "local position to the player's world position plus [0, 3, -12].\n"
                    "13. Build, attach, save and play-test:\n";
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
                    "local position to the player's world position plus [0, " + formatNumber(height) + ", 0].\n"
                    "13. Build, attach, save and play-test:\n";
                text += buildLoop(game, "scenes/" + game + ".vfScene");
                return userMessage(std::move(text));
            };
            registry.add(std::move(prompt));
        }
    }

    void registerCorePrompts(PromptRegistry& registry)
    {
        registerPlatformerTemplate(registry);
        registerTopDownTemplate(registry);
    }
}
