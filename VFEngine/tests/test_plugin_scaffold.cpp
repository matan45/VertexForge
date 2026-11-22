#include "doctest.h"

#include "core/PluginScaffolder.hpp"
#include "api/PluginVersion.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

// VK-1284: Plugin Scaffolding Tool — CPU-only tests over the header-only
// generator (name validation, the three emitted files, no-clobber behavior).

namespace
{
    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    struct TempDir
    {
        std::filesystem::path path;

        TempDir()
        {
            path = std::filesystem::temp_directory_path() / "vf_scaffold_test";
            std::filesystem::remove_all(path);
            std::filesystem::create_directories(path);
        }

        ~TempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };
}

TEST_CASE("plugin scaffold: name validation")
{
    using plugin::scaffold::isValidName;

    SUBCASE("accepts PascalCase identifiers")
    {
        CHECK(isValidName("MyPlugin"));
        CHECK(isValidName("HexTerrain2"));
        CHECK(isValidName("A"));
        CHECK(isValidName("RTSGameplay"));
    }

    SUBCASE("rejects invalid identifiers")
    {
        CHECK_FALSE(isValidName(""));
        CHECK_FALSE(isValidName("myPlugin"));      // lowercase start
        CHECK_FALSE(isValidName("2Foo"));          // digit start
        CHECK_FALSE(isValidName("my-plugin"));     // hyphen
        CHECK_FALSE(isValidName("Foo Bar"));       // space
        CHECK_FALSE(isValidName("Foo_Bar"));       // underscore
        CHECK_FALSE(isValidName("Foo.Bar"));       // dot
        CHECK_FALSE(isValidName(std::string(65, 'A'))); // too long
    }
}

TEST_CASE("plugin scaffold: descriptor JSON has engine apiVersion and sane defaults")
{
    plugin::scaffold::Options opts;
    opts.name = "ScaffoldTest";
    opts.author = "UnitTest";
    opts.capabilities = {"graphics", "input"};

    auto json = nlohmann::json::parse(plugin::scaffold::makeDescriptorJson(opts));

    CHECK(json["apiVersion"].get<uint32_t>() == plugin::VF_PLUGIN_API_VERSION);
    CHECK(json["name"] == "ScaffoldTest");
    CHECK(json["author"] == "UnitTest");
    CHECK(json["library"] == "ScaffoldTest.dll");
    CHECK(json["version"] == "1.0.0");
    CHECK(json["enabled"] == true);
    CHECK(json["loadOrder"] == 100);
    CHECK(json["capabilities"] == nlohmann::json({"graphics", "input"}));
    CHECK(json["dependencies"].is_array());
    CHECK(json["dependencies"].empty());
}

TEST_CASE("plugin scaffold: premake one-liner uses vfPluginProject")
{
    plugin::scaffold::Options opts;
    opts.name = "ScaffoldTest";

    SUBCASE("without editor window the imgui link stays commented")
    {
        auto lua = plugin::scaffold::makePremakeLua(opts);
        CHECK(lua.find("vfPluginProject(\"ScaffoldTest\")") != std::string::npos);
        CHECK(lua.find("-- links { \"imgui\" }") != std::string::npos);
    }

    SUBCASE("with editor window the imgui link is active")
    {
        opts.withEditorWindow = true;
        auto lua = plugin::scaffold::makePremakeLua(opts);
        CHECK(lua.find("vfPluginProject(\"ScaffoldTest\")") != std::string::npos);
        CHECK(lua.find("\n   links { \"imgui\" }") != std::string::npos);
    }
}

TEST_CASE("plugin scaffold: generated cpp follows the IPlugin pattern")
{
    plugin::scaffold::Options opts;
    opts.name = "ScaffoldTest";
    opts.author = "UnitTest";

    SUBCASE("default emits component registration")
    {
        auto cpp = plugin::scaffold::makeCppSource(opts);
        CHECK(cpp.find("class ScaffoldTest : public plugin::IPlugin") != std::string::npos);
        CHECK(cpp.find("VF_IMPLEMENT_PLUGIN(ScaffoldTest)") != std::string::npos);
        CHECK(cpp.find("struct ScaffoldTestComponent") != std::string::npos);
        CHECK(cpp.find("registerNativeComponent<ScaffoldTestComponent>(\"ScaffoldTestComponent\")") != std::string::npos);
        CHECK(cpp.find(".data<&ScaffoldTestComponent::value>(\"value\")") != std::string::npos);
        CHECK(cpp.find("\"ScaffoldTest\", \"UnitTest\"") != std::string::npos);
        // no editor window bits without the toggle
        CHECK(cpp.find("registerEditorWindow") == std::string::npos);
        CHECK(cpp.find("imguiHandler") == std::string::npos);
    }

    SUBCASE("component toggle off removes the example")
    {
        opts.withExampleComponent = false;
        auto cpp = plugin::scaffold::makeCppSource(opts);
        CHECK(cpp.find("ScaffoldTestComponent") == std::string::npos);
        CHECK(cpp.find("registerNativeComponent") == std::string::npos);
        CHECK(cpp.find("VF_IMPLEMENT_PLUGIN(ScaffoldTest)") != std::string::npos);
    }

    SUBCASE("editor window toggle emits the ImguiWindow registration")
    {
        opts.withEditorWindow = true;
        auto cpp = plugin::scaffold::makeCppSource(opts);
        CHECK(cpp.find("class ScaffoldTestWindow : public controllers::imguiHandler::ImguiWindow") != std::string::npos);
        CHECK(cpp.find("ImGui::SetCurrentContext(ctx->getImGuiContext())") != std::string::npos);
        CHECK(cpp.find("registerEditorWindow(std::make_shared<ScaffoldTestWindow>(), \"ScaffoldTest\")") != std::string::npos);
        CHECK(cpp.find("hasCapability(std::string(plugin::capability::editor))") != std::string::npos);
    }
}

TEST_CASE("plugin scaffold: createPlugin writes the three files and refuses clobbering")
{
    TempDir tempDir;

    plugin::scaffold::Options opts;
    opts.name = "ScaffoldTest";
    opts.capabilities = {"graphics"};

    SUBCASE("happy path creates the folder with three files")
    {
        auto error = plugin::scaffold::createPlugin(tempDir.path, opts);
        CHECK(error.empty());

        const auto pluginDir = tempDir.path / "ScaffoldTest";
        CHECK(std::filesystem::exists(pluginDir / "premake5.lua"));
        CHECK(std::filesystem::exists(pluginDir / "ScaffoldTest.cpp"));
        CHECK(std::filesystem::exists(pluginDir / "ScaffoldTest.vfplugin"));

        // descriptor round-trips as valid JSON with the engine apiVersion
        auto json = nlohmann::json::parse(readFile(pluginDir / "ScaffoldTest.vfplugin"));
        CHECK(json["apiVersion"].get<uint32_t>() == plugin::VF_PLUGIN_API_VERSION);

        // emitted files match the in-memory templates
        CHECK(readFile(pluginDir / "premake5.lua") == plugin::scaffold::makePremakeLua(opts));
        CHECK(readFile(pluginDir / "ScaffoldTest.cpp") == plugin::scaffold::makeCppSource(opts));
    }

    SUBCASE("existing folder is rejected")
    {
        CHECK(plugin::scaffold::createPlugin(tempDir.path, opts).empty());
        auto error = plugin::scaffold::createPlugin(tempDir.path, opts);
        CHECK_FALSE(error.empty());
        CHECK(error.find("already exists") != std::string::npos);
    }

    SUBCASE("invalid name is rejected before touching the filesystem")
    {
        opts.name = "my-plugin";
        auto error = plugin::scaffold::createPlugin(tempDir.path, opts);
        CHECK_FALSE(error.empty());
        CHECK_FALSE(std::filesystem::exists(tempDir.path / "my-plugin"));
    }
}

// VK-1732: the in-tree flavour stays byte-identical; an installed engine gets the Standalone flavour.

TEST_CASE("plugin scaffold: the in-tree premake5.lua is unchanged")
{
    plugin::scaffold::Options opts;
    opts.name = "ScaffoldTest";
    CHECK(opts.flavor == plugin::scaffold::Flavor::InTree);

    CHECK(plugin::scaffold::makePremakeLua(opts) ==
          "-- ScaffoldTest plugin - auto-discovered by the root premake5.lua.\n"
          "-- Common setup (sdk/ includes, defines, DLL postbuild copy) comes from plugins/plugin_sdk.lua.\n"
          "vfPluginProject(\"ScaffoldTest\")\n"
          "   -- links { \"imgui\" }   -- uncomment if this plugin registers an editor window\n");

    opts.withEditorWindow = true;
    CHECK(plugin::scaffold::makePremakeLua(opts) ==
          "-- ScaffoldTest plugin - auto-discovered by the root premake5.lua.\n"
          "-- Common setup (sdk/ includes, defines, DLL postbuild copy) comes from plugins/plugin_sdk.lua.\n"
          "vfPluginProject(\"ScaffoldTest\")\n"
          "   links { \"imgui\" }   -- editor UI window\n");
}

TEST_CASE("plugin scaffold: luaLongString reads back as the text")
{
    using plugin::scaffold::luaLongString;

    CHECK(luaLongString("") == "[[]]");
    CHECK(luaLongString("C:/Program Files/VertexForge/sdk") == "[[C:/Program Files/VertexForge/sdk]]");
    // the level-0 closing bracket inside the text
    CHECK(luaLongString("a]]b") == "[=[a]]b]=]");
    // a trailing ']' would join the closing bracket
    CHECK(luaLongString("a]") == "[=[a]]=]");
    // "]=" at the end with level 1 would close early too; level 0 is fine there
    CHECK(luaLongString("a]=") == "[[a]=]]");
    // both lower levels occur
    CHECK(luaLongString("x]=]y]]") == "[==[x]=]y]]]==]");
    // opening brackets never matter
    CHECK(luaLongString("[[x[=[") == "[[[[x[=[]]");
}

TEST_CASE("plugin scaffold: the Standalone premake5.lua builds against the engine SDK")
{
    plugin::scaffold::Options opts;
    opts.name = "ScaffoldTest";
    opts.flavor = plugin::scaffold::Flavor::Standalone;
    opts.sdkDir = std::filesystem::path(L"C:\\Users\\Dev\\AppData\\Local\\Programs\\VertexForge\\sdk");
    opts.configuration = "Release";

    const std::string lua = plugin::scaffold::makePremakeLua(opts);
    CHECK(lua == plugin::scaffold::makeStandalonePremakeLua(opts));
    const auto has = [&lua](const std::string& text) { return lua.find(text) != std::string::npos; };

    SUBCASE("its own workspace, not the in-tree one-liner")
    {
        CHECK_FALSE(has("vfPluginProject"));
        CHECK(has("local PLUGIN_NAME = \"ScaffoldTest\"\n"));
        CHECK(has("workspace (PLUGIN_NAME)\n"));
        CHECK(has("project (PLUGIN_NAME)\n"));
        CHECK(has("   kind \"SharedLib\"\n"));
        CHECK(has("   cppdialect \"C++20\"\n"));
    }

    SUBCASE("the SDK: VERTEXFORGE_SDK, else the engine's sdk folder with forward slashes")
    {
        CHECK(has("local sdkDir = os.getenv(\"VERTEXFORGE_SDK\") or "
                  "[[C:/Users/Dev/AppData/Local/Programs/VertexForge/sdk]]\n"));
        CHECK(has("if not os.isdir(sdkDir .. \"/include/VFEngine/plugin\") then\n   error("));
        CHECK(has("local vulkanSDK = os.getenv(\"VULKAN_SDK\")\nif not vulkanSDK then\n   error("));
        CHECK(has("      sdkDir .. \"/include/VFEngine/plugin\",\n"));
        CHECK(has("      sdkDir .. \"/include/VFEngine/core/controllers\",\n"));
        CHECK(has("      sdkDir .. \"/deps/mType\",\n"));
        CHECK(has("      vulkanSDK .. \"/Include\"\n"));
        CHECK(has("   libdirs { sdkDir .. \"/lib/%{cfg.buildcfg}\" }\n"));
    }

    SUBCASE("one configuration, the engine's ABI")
    {
        CHECK(has("   configurations { \"Release\" }\n"));
        CHECK_FALSE(has("\"Debug\""));
        CHECK_FALSE(has("\"Development\""));
        CHECK(has("   platforms { \"x64\" }\n"));
        CHECK(has("      toolset \"v145\"\n"));
        CHECK(has("      buildoptions { \"/utf-8\", \"/MP\" }\n"));
        CHECK(has("      defines { \"VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1\", \"GLM_FORCE_DEPTH_ZERO_TO_ONE\" }\n"));
        CHECK(has("   runtime \"Release\"\n"));
        CHECK(has("   defines { \"NDEBUG\" }\n"));
        CHECK(has("   optimize \"On\"\n"));
        CHECK_FALSE(has("symbols"));
    }

    SUBCASE("outputs inside the folder, the DLL copied beside the .vfplugin")
    {
        CHECK(has("   targetdir \"bin/%{cfg.buildcfg}\"\n"));
        CHECK(has("   objdir \"obj/%{cfg.buildcfg}\"\n"));
        CHECK(has("   postbuildcommands {\n"
                  "      \"{COPY} \\\"%{cfg.buildtarget.abspath}\\\" \\\"%{prj.location}/\\\"\"\n"
                  "   }\n"));
    }

    SUBCASE("imgui is linked only for an editor window")
    {
        CHECK(has("   -- links { \"imgui\" }"));
        opts.withEditorWindow = true;
        const std::string withWindow = plugin::scaffold::makeStandalonePremakeLua(opts);
        CHECK(withWindow.find("\n   links { \"imgui\" }") != std::string::npos);
    }

    SUBCASE("Debug and Development builds match those engine configurations")
    {
        opts.configuration = "Debug";
        const std::string debug = plugin::scaffold::makeStandalonePremakeLua(opts);
        CHECK(debug.find("   configurations { \"Debug\" }\n") != std::string::npos);
        CHECK(debug.find("   runtime \"Debug\"\n") != std::string::npos);  // the engine's Debug uses /MDd
        CHECK(debug.find("   defines { \"DEBUG\" }\n") != std::string::npos);
        CHECK(debug.find("   symbols \"On\"\n") != std::string::npos);
        CHECK(debug.find("optimize") == std::string::npos);

        opts.configuration = "Development";
        const std::string development = plugin::scaffold::makeStandalonePremakeLua(opts);
        CHECK(development.find("   configurations { \"Development\" }\n") != std::string::npos);
        CHECK(development.find("   runtime \"Release\"\n") != std::string::npos);
        CHECK(development.find("   defines { \"NDEBUG\", \"VF_DEVELOPMENT\" }\n") != std::string::npos);
        CHECK(development.find("   optimize \"On\"\n") != std::string::npos);
        CHECK(development.find("   symbols \"On\"\n") != std::string::npos);
    }

    SUBCASE("a non-ASCII SDK path is written as UTF-8")
    {
        opts.sdkDir = std::filesystem::path(u8"C:/\u05DE\u05E9\u05EA\u05DE\u05E9/VertexForge/sdk");
        const std::string unicode = plugin::scaffold::makeStandalonePremakeLua(opts);
        const std::u8string expected = u8"[[C:/\u05DE\u05E9\u05EA\u05DE\u05E9/VertexForge/sdk]]";
        CHECK(unicode.find(std::string(expected.begin(), expected.end())) != std::string::npos);
    }
}

TEST_CASE("plugin scaffold: createPlugin writes a Standalone plugin's three files")
{
    TempDir tempDir;

    plugin::scaffold::Options opts;
    opts.name = "ScaffoldTest";
    opts.capabilities = {"graphics"};
    opts.flavor = plugin::scaffold::Flavor::Standalone;
    opts.sdkDir = tempDir.path / "sdk";
    opts.configuration = "Development";

    SUBCASE("the folder holds exactly premake5.lua, the .cpp and the .vfplugin")
    {
        CHECK(plugin::scaffold::createPlugin(tempDir.path, opts).empty());

        const auto pluginDir = tempDir.path / "ScaffoldTest";
        int fileCount = 0;
        for (const auto& entry : std::filesystem::directory_iterator(pluginDir))
        {
            CHECK(entry.is_regular_file());
            ++fileCount;
        }
        CHECK(fileCount == 3);
        CHECK(readFile(pluginDir / "premake5.lua") == plugin::scaffold::makeStandalonePremakeLua(opts));
        // the source and the descriptor are the in-tree flavour's
        CHECK(readFile(pluginDir / "ScaffoldTest.cpp") == plugin::scaffold::makeCppSource(opts));
        CHECK(readFile(pluginDir / "ScaffoldTest.vfplugin") == plugin::scaffold::makeDescriptorJson(opts));
    }

    SUBCASE("an unknown SDK folder or configuration is refused before touching the filesystem")
    {
        plugin::scaffold::Options noSdk = opts;
        noSdk.sdkDir.clear();
        CHECK_FALSE(plugin::scaffold::createPlugin(tempDir.path, noSdk).empty());

        plugin::scaffold::Options badConfig = opts;
        badConfig.configuration = "Shipping";
        const std::string error = plugin::scaffold::createPlugin(tempDir.path, badConfig);
        CHECK(error.find("Shipping") != std::string::npos);

        CHECK_FALSE(std::filesystem::exists(tempDir.path / "ScaffoldTest"));
    }
}
