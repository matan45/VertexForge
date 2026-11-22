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
// VK-1732: an installed engine has no root premake5.lua, and its plugins/ folder
// is the MSI's. There the editor makes a Standalone plugin in the user's
// Documents\VertexForge\Plugins: the same .cpp and .vfplugin, plus a premake5.lua
// with its own workspace that builds against the engine's sdk/ (one
// configuration, the editor's own, so the ABI matches) and copies the DLL beside
// the .vfplugin, where the editor finds it at its next start.
//
// Header-only on purpose: the Editor uses it from PluginManagerWindow, and the
// Tests project can exercise the templates without linking the Plugin module.
namespace plugin::scaffold
{
    enum class Flavor
    {
        InTree,     // plugins/<Name>/ in the dev tree: vfPluginProject() from plugins/plugin_sdk.lua
        Standalone  // anywhere else: its own workspace against an installed engine's sdk/
    };

    struct Options
    {
        std::string name;                       // PascalCase identifier — C++ class + premake project token
        std::string author = "VertexForge";
        std::vector<std::string> capabilities;  // values from plugin::capability
        bool withExampleComponent = true;
        bool withEditorWindow = false;          // adds links{"imgui"}, an ImguiWindow subclass + registration
        Flavor flavor = Flavor::InTree;
        std::filesystem::path sdkDir;           // Standalone: the engine's sdk/ folder, absolute
        std::string configuration = "Release";  // Standalone: the editor's own (Debug, Development or Release)
    };

    inline bool isKnownConfiguration(const std::string& configuration)
    {
        return configuration == "Debug" || configuration == "Development" || configuration == "Release";
    }

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

    // A Lua long string literal that reads back as `text` exactly: [[...]], [=[...]=], [==[...]==], ...
    // at the first level whose closing bracket occurs nowhere before the end, also not formed by the
    // text's last characters and the bracket together (text "a]" needs [=[a]]=]). Lua drops a newline
    // right after the opening bracket; a path never starts with one.
    inline std::string luaLongString(const std::string& text)
    {
        for (std::size_t level = 0;; ++level)
        {
            const std::string equals(level, '=');
            const std::string close = "]" + equals + "]";
            if ((text + close).find(close) == text.size())
                return "[" + equals + "[" + text + close;
        }
    }

    // premake5.lua of a Standalone plugin, based on sdk/template/premake5.lua: its own workspace with ONE
    // configuration (the editor's own: the ABI that is sure to match the editor that loads the plugin),
    // the engine's sdk/ as the default SDK, and a postbuild copy of the DLL beside the .vfplugin.
    inline std::string makeStandalonePremakeLua(const Options& opts)
    {
        const std::string& n = opts.name;
        const std::string& config = opts.configuration;
        const std::u8string sdkUtf8 = opts.sdkDir.generic_u8string();
        const std::string sdk(sdkUtf8.begin(), sdkUtf8.end());

        std::string out;
        out += "-- " + n + ": a VertexForge plugin made by the editor's New Plugin, built against the engine's\n";
        out += "-- plugin SDK. In this folder run `premake5 vs2022`, then build " + n + ".sln (" + config + "|x64)\n";
        out += "-- with the editor closed: the build copies " + n + ".dll beside " + n + ".vfplugin, and the\n";
        out += "-- editor loads it at its next start.\n";
        out += "-- Needs Visual Studio 2026 (toolset v145), premake5 and the Vulkan SDK (VULKAN_SDK).\n";
        out += "-- VERTEXFORGE_SDK, when set, names another engine's sdk folder. The plugin must be built in the\n";
        out += "-- configuration of the editor that loads it (" + config + ").\n";
        out += "\n";
        out += "local PLUGIN_NAME = \"" + n + "\"\n";
        out += "\n";
        out += "local sdkDir = os.getenv(\"VERTEXFORGE_SDK\") or " + luaLongString(sdk) + "\n";
        out += "local vulkanSDK = os.getenv(\"VULKAN_SDK\")\n";
        out += "if not vulkanSDK then\n";
        out += "   error(\"VULKAN_SDK is not set: install the Vulkan SDK\")\n";
        out += "end\n";
        out += "if not os.isdir(sdkDir .. \"/include/VFEngine/plugin\") then\n";
        out += "   error(\"No VertexForge plugin SDK in \" .. sdkDir .. \" (set VERTEXFORGE_SDK to an engine's sdk folder)\")\n";
        out += "end\n";
        out += "\n";
        out += "workspace (PLUGIN_NAME)\n";
        out += "   configurations { \"" + config + "\" }\n";
        out += "   platforms { \"x64\" }\n";
        out += "   location \".\"\n";
        out += "\n";
        out += "   -- The ABI must match the engine: MSVC v145, C++20, the editor's C runtime, /utf-8 (spdlog/fmt\n";
        out += "   -- requirement), the same engine-wide defines.\n";
        out += "   filter \"system:windows\"\n";
        out += "      systemversion \"latest\"\n";
        out += "      toolset \"v145\"\n";
        out += "   filter \"language:C++\"\n";
        out += "      buildoptions { \"/utf-8\", \"/MP\" }\n";
        out += "      defines { \"VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1\", \"GLM_FORCE_DEPTH_ZERO_TO_ONE\" }\n";
        out += "   filter {}\n";
        out += "\n";
        out += "project (PLUGIN_NAME)\n";
        out += "   kind \"SharedLib\"\n";
        out += "   language \"C++\"\n";
        out += "   cppdialect \"C++20\"\n";
        out += "   location \".\"\n";
        out += "   targetdir \"bin/%{cfg.buildcfg}\"\n";
        out += "   objdir \"obj/%{cfg.buildcfg}\"\n";
        // The engine's Debug configuration uses the debug C runtime (/MDd), Development and Release /MD.
        out += std::string("   runtime \"") + (config == "Debug" ? "Debug" : "Release") + "\"\n";
        if (config == "Debug")
        {
            out += "   defines { \"DEBUG\" }\n";
            out += "   symbols \"On\"\n";
        }
        else if (config == "Development")
        {
            out += "   defines { \"NDEBUG\", \"VF_DEVELOPMENT\" }\n";
            out += "   optimize \"On\"\n";
            out += "   symbols \"On\"\n";
        }
        else
        {
            out += "   defines { \"NDEBUG\" }\n";
            out += "   optimize \"On\"\n";
        }
        out += "\n";
        out += "   files { \"**.hpp\", \"**.cpp\" }\n";
        out += "\n";
        out += "   includedirs {\n";
        out += "      sdkDir .. \"/include/VFEngine/plugin\",\n";
        out += "      sdkDir .. \"/include/VFEngine/utilities\",\n";
        out += "      sdkDir .. \"/include/VFEngine/services\",\n";
        out += "      sdkDir .. \"/include/VFEngine/core/controllers\",\n";
        out += "      sdkDir .. \"/include/VFEngine/import\",\n";
        out += "      sdkDir .. \"/deps/glm\",\n";
        out += "      sdkDir .. \"/deps/entt\",\n";
        out += "      sdkDir .. \"/deps/json\",\n";
        out += "      sdkDir .. \"/deps/spdlog\",\n";
        out += "      sdkDir .. \"/deps/imgui\",\n";
        out += "      sdkDir .. \"/deps/mType\",\n";
        out += "      vulkanSDK .. \"/Include\"\n";
        out += "   }\n";
        out += "\n";
        out += "   libdirs { sdkDir .. \"/lib/%{cfg.buildcfg}\" }\n";
        if (opts.withEditorWindow)
            out += "   links { \"imgui\" }   -- editor UI window\n";
        else
            out += "   -- links { \"imgui\" }   -- uncomment if this plugin registers an editor window\n";
        out += "\n";
        out += "   defines { \"_CRT_SECURE_NO_WARNINGS\" }\n";
        out += "\n";
        out += "   -- The editor loads the DLL from beside the .vfplugin.\n";
        out += "   postbuildcommands {\n";
        out += "      \"{COPY} \\\"%{cfg.buildtarget.abspath}\\\" \\\"%{prj.location}/\\\"\"\n";
        out += "   }\n";
        return out;
    }

    inline std::string makePremakeLua(const Options& opts)
    {
        if (opts.flavor == Flavor::Standalone)
            return makeStandalonePremakeLua(opts);

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

        if (opts.flavor == Flavor::Standalone)
        {
            if (opts.sdkDir.empty())
                return "The engine's plugin SDK folder is not known";
            if (!isKnownConfiguration(opts.configuration))
                return "Unknown build configuration '" + opts.configuration + "' (Debug, Development or Release)";
        }

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
