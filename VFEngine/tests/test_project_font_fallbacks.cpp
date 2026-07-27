#include <doctest.h>

#include "serialization/ProjectSerialization.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    struct TempProjectFile
    {
        fs::path directory;
        fs::path path;

        explicit TempProjectFile(const char* name)
        {
            directory = fs::temp_directory_path() / name;
            std::error_code ec;
            fs::remove_all(directory, ec);
            fs::create_directories(directory, ec);
            path = directory / "Fallbacks.vfproj";
        }

        ~TempProjectFile()
        {
            std::error_code ec;
            fs::remove_all(directory, ec);
        }
    };

    config::ProjectConfig makeProject(const fs::path& workingDirectory)
    {
        config::ProjectConfig project;
        project.projectName = "FallbackProject";
        project.version = "1.0";
        project.workingDirectory = workingDirectory.string();
        project.startupScene = "Startup.vfScene";
        return project;
    }

    void writeJson(const fs::path& path, const json& value)
    {
        std::ofstream file(path);
        file << value.dump(2);
    }

    json requiredProjectJson(const fs::path& workingDirectory)
    {
        return {
            {"schemaVersion", config::ProjectSchemaVersion::toString()},
            {"projectName", "FallbackProject"},
            {"version", "1.0"},
            {"workingDirectory", workingDirectory.string()},
            {"startupScene", "Startup.vfScene"}
        };
    }
}

TEST_SUITE("ProjectFontFallbacks")
{
    TEST_CASE("font fallback paths round trip in authored order")
    {
        TempProjectFile temp("VertexForge_ProjectFontFallbacks_RoundTrip");
        auto project = makeProject(temp.directory);
        project.fontFallbackChain = {
            "Assets/Fonts/Primary.vfFont",
            "Assets/Fonts/CJK.vfFont",
            "Assets/Fonts/Emoji.vfFont"
        };

        REQUIRE(serialization::ProjectSerialization::saveProject(project, temp.path.string()));
        const auto loaded = serialization::ProjectSerialization::loadProject(temp.path.string());
        REQUIRE(loaded.has_value());
        CHECK(loaded->fontFallbackChain == project.fontFallbackChain);

        std::ifstream file(temp.path);
        json saved;
        file >> saved;
        REQUIRE(saved.contains("fontFallbackChain"));
        CHECK(saved["fontFallbackChain"].is_array());
    }

    TEST_CASE("empty font fallback chains are omitted and missing keys load empty")
    {
        TempProjectFile temp("VertexForge_ProjectFontFallbacks_Empty");
        const auto project = makeProject(temp.directory);
        REQUIRE(serialization::ProjectSerialization::saveProject(project, temp.path.string()));

        std::ifstream file(temp.path);
        json saved;
        file >> saved;
        CHECK_FALSE(saved.contains("fontFallbackChain"));

        const auto loaded = serialization::ProjectSerialization::loadProject(temp.path.string());
        REQUIRE(loaded.has_value());
        CHECK(loaded->fontFallbackChain.empty());
    }

    TEST_CASE("invalid font fallback values are ignored without rejecting the project")
    {
        TempProjectFile temp("VertexForge_ProjectFontFallbacks_Invalid");

        SUBCASE("wrong field type")
        {
            auto value = requiredProjectJson(temp.directory);
            value["fontFallbackChain"] = "Assets/Fonts/NotAnArray.vfFont";
            writeJson(temp.path, value);

            const auto loaded = serialization::ProjectSerialization::loadProject(temp.path.string());
            REQUIRE(loaded.has_value());
            CHECK(loaded->fontFallbackChain.empty());
        }

        SUBCASE("non-string entries")
        {
            auto value = requiredProjectJson(temp.directory);
            value["fontFallbackChain"] = json::array({
                "Assets/Fonts/Primary.vfFont", 42, nullptr,
                "Assets/Fonts/Emoji.vfFont"
            });
            writeJson(temp.path, value);

            const auto loaded = serialization::ProjectSerialization::loadProject(temp.path.string());
            REQUIRE(loaded.has_value());
            REQUIRE(loaded->fontFallbackChain.size() == 2);
            CHECK(loaded->fontFallbackChain[0] == "Assets/Fonts/Primary.vfFont");
            CHECK(loaded->fontFallbackChain[1] == "Assets/Fonts/Emoji.vfFont");
        }
    }

    TEST_CASE("serialization enforces the three authored fallback limit")
    {
        TempProjectFile temp("VertexForge_ProjectFontFallbacks_Max");
        auto project = makeProject(temp.directory);
        project.fontFallbackChain = {"one.vfFont", "two.vfFont", "three.vfFont", "four.vfFont"};

        REQUIRE(serialization::ProjectSerialization::saveProject(project, temp.path.string()));
        const auto loaded = serialization::ProjectSerialization::loadProject(temp.path.string());
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->fontFallbackChain.size() == config::ProjectConfig::maxFontFallbacks);
        CHECK(loaded->fontFallbackChain.back() == "three.vfFont");
    }
}
