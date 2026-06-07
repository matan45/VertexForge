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
