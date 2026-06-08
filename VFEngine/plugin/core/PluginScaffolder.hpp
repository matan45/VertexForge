#pragma once
#include "api/PluginVersion.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// VK-1284: generates a ready-to-build plugin folder (premake5.lua + <Name>.cpp +
// <Name>.vfplugin) under plugins/. Everything else — sdk/ include paths, defines,
// output dirs, the postbuild DLL copy and root-solution discovery — is already
// handled by plugins/plugin_sdk.lua and the root premake5.lua, so these three
// files are the only authored artifacts a new plugin needs.
//
// Header-only on purpose: the Editor uses it from PluginManagerWindow, and the
// Tests project can exercise the templates without linking the Plugin module.
namespace plugin::scaffold
{
    struct Options
    {
        std::string name;                       // PascalCase identifier — C++ class + premake project token
        std::string author = "VertexForge";
        std::vector<std::string> capabilities;  // values from plugin::capability
        bool withExampleComponent = true;
        bool withEditorWindow = false;          // adds links{"imgui"}, an ImguiWindow subclass + registration
    };

    // ^[A-Z][A-Za-z0-9]*$ — the name becomes the C++ class name, the premake
    // project token, the DLL base name and the descriptor name, so it must be
    // a plain PascalCase identifier.
    inline bool isValidName(const std::string& name)
    {
        if (name.empty() || name.size() > 64)
            return false;
        if (name[0] < 'A' || name[0] > 'Z')
            return false;
        for (char c : name)
        {
            const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            if (!alnum)
                return false;
        }
        return true;
    }

    inline std::string makePremakeLua(const Options& opts)
    {
        std::string out;
        out += "-- " + opts.name + " plugin - auto-discovered by the root premake5.lua.\n";
        out += "-- Common setup (sdk/ includes, defines, DLL postbuild copy) comes from plugins/plugin_sdk.lua.\n";
        out += "vfPluginProject(\"" + opts.name + "\")\n";
        if (opts.withEditorWindow)
            out += "   links { \"imgui\" }   -- editor UI window\n";
        else
            out += "   -- links { \"imgui\" }   -- uncomment if this plugin registers an editor window\n";
        return out;
    }

    inline std::string makeDescriptorJson(const Options& opts)
    {
        nlohmann::json json;
        json["apiVersion"] = VF_PLUGIN_API_VERSION;
        json["author"] = opts.author;
        json["capabilities"] = opts.capabilities;
        json["dependencies"] = nlohmann::json::array();
        json["description"] = opts.name + " plugin";
        json["enabled"] = true;
        json["library"] = opts.name + ".dll";
        json["loadOrder"] = 100;
        json["name"] = opts.name;
        json["version"] = "1.0.0";
        return json.dump(4) + "\n";
    }

    inline std::string makeCppSource(const Options& opts)
    {
        const std::string& n = opts.name;
        std::string out;

        out += "#include \"api/IPlugin.hpp\"\n";
        out += "#include \"api/PluginExport.hpp\"\n";
        out += "#include \"api/PluginContext.hpp\"\n";
        if (opts.withEditorWindow)
        {
            out += "#include \"imguiHandler/ImguiWindow.hpp\"\n";
            out += "#include <imgui.h>\n";
            out += "#include <memory>\n";
        }
        if (opts.withExampleComponent)
        {
            out += "#include <entt/entt.hpp>\n";
            out += "#include <glm/glm.hpp>\n";
        }
        out += "#include <string>\n";
        out += "\n";

        if (opts.withExampleComponent)
        {
            out += "// Native component struct - registered with EnTT meta for cross-DLL type\n";
            out += "// identity. Shows up in the inspector and is reachable from mType scripts\n";
            out += "// via PluginComponent::*(\"" + n + "Component\", ...).\n";
            out += "struct " + n + "Component\n";
            out += "{\n";
            out += "    int value = 0;\n";
            out += "    float weight = 1.0f;\n";
            out += "    glm::vec3 offset{0.0f, 0.0f, 0.0f};\n";
            out += "};\n";
            out += "\n";
        }

        if (opts.withEditorWindow)
        {
            out += "// Editor panel - registered via PluginContext::registerEditorWindow, shown\n";
            out += "// under the editor's Plugins menu. draw() runs inside the engine ImGui frame.\n";
            out += "class " + n + "Window : public controllers::imguiHandler::ImguiWindow\n";
            out += "{\n";
            out += "public:\n";
            out += "    void draw() override\n";
            out += "    {\n";
            out += "        if (!ImGui::Begin(\"" + n + "\"))\n";
            out += "        {\n";
            out += "            ImGui::End();\n";
            out += "            return;\n";
            out += "        }\n";
            out += "        ImGui::TextUnformatted(\"" + n + " plugin window\");\n";
            out += "        ImGui::End();\n";
            out += "    }\n";
            out += "};\n";
            out += "\n";
        }

        out += "class " + n + " : public plugin::IPlugin\n";
        out += "{\n";
        out += "public:\n";
        out += "    plugin::PluginInfo getInfo() const override\n";
        out += "    {\n";
        out += "        return {\"" + n + "\", \"" + opts.author + "\", \"" + n + " plugin\", 1, 0, 0};\n";
        out += "    }\n";
        out += "\n";
        out += "    bool onInitialize(plugin::PluginContext* context) override\n";
        out += "    {\n";
        out += "        ctx = context;\n";

        if (opts.withExampleComponent)
        {
            out += "\n";
            out += "        ctx->registerNativeComponent<" + n + "Component>(\"" + n + "Component\")\n";
            out += "            .data<&" + n + "Component::value>(\"value\")\n";
            out += "            .data<&" + n + "Component::weight>(\"weight\")\n";
            out += "            .data<&" + n + "Component::offset>(\"offset\");\n";
        }

        if (opts.withEditorWindow)
        {
            out += "\n";
            out += "        if (ctx->hasCapability(std::string(plugin::capability::editor)))\n";
            out += "        {\n";
            out += "            ImGui::SetCurrentContext(ctx->getImGuiContext());\n";
            out += "            ctx->registerEditorWindow(std::make_shared<" + n + "Window>(), \"" + n + "\");\n";
            out += "        }\n";
        }

        out += "\n";
        out += "        ctx->logInfo(\"" + n + " initialized\");\n";
        out += "        return true;\n";
        out += "    }\n";
        out += "\n";
        out += "    void onUpdate(float deltaTime) override\n";
        out += "    {\n";
        out += "        (void)deltaTime;\n";
        out += "    }\n";
        out += "\n";
        out += "    void onShutdown() override\n";
        out += "    {\n";
        out += "        ctx->logInfo(\"" + n + " shutdown\");\n";
        out += "    }\n";
        out += "\n";
        out += "private:\n";
        out += "    plugin::PluginContext* ctx = nullptr;\n";
        out += "};\n";
        out += "\n";
        out += "VF_IMPLEMENT_PLUGIN(" + n + ")\n";

        return out;
    }

    // Creates pluginsDir/<Name>/ with the three authored files.
    // Returns an empty string on success, a human-readable error otherwise.
    // Refuses to touch an existing folder (no clobbering authored plugins).
    inline std::string createPlugin(const std::filesystem::path& pluginsDir, const Options& opts)
    {
        if (!isValidName(opts.name))
            return "Invalid plugin name - use a PascalCase identifier (e.g. MyPlugin)";

        if (pluginsDir.empty())
            return "Plugins directory is not resolved";

        std::error_code ec;
        const auto pluginDir = pluginsDir / opts.name;
        if (std::filesystem::exists(pluginDir, ec))
            return "Folder already exists: " + pluginDir.string();

        if (!std::filesystem::create_directories(pluginDir, ec) || ec)
            return "Failed to create " + pluginDir.string();

        auto writeFile = [&pluginDir](const std::filesystem::path& path, const std::string& content) {
            std::ofstream file(path, std::ios::binary);
            if (!file.is_open())
                return false;
            file << content;
            return file.good();
        };

        if (!writeFile(pluginDir / "premake5.lua", makePremakeLua(opts)))
            return "Failed to write premake5.lua";
        if (!writeFile(pluginDir / (opts.name + ".cpp"), makeCppSource(opts)))
            return "Failed to write " + opts.name + ".cpp";
        if (!writeFile(pluginDir / (opts.name + ".vfplugin"), makeDescriptorJson(opts)))
            return "Failed to write " + opts.name + ".vfplugin";

        return {};
    }
}
