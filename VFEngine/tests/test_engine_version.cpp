#include <doctest.h>

#include "api/PluginVersion.hpp"
#include "config/Config.hpp"
#include "config/ProjectConfig.hpp"
#include "serialization/ProjectSerialization.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include "test_repo_scan_helpers.hpp" // pulls <windows.h>: keep it last

// ============================================================
// VK-1680: the repo-root VERSION file is the ONLY engine version source.
// premake (tools/engine_version.lua) turns it into EngineVersion.generated.hpp,
// which Config.hpp's `Version` reads, and the Editor postbuild writes engine.json
// beside Editor.exe from the same sources. These tests catch a stale generation
// (VERSION edited without re-running premake5 vs2022) and pin the manifest
// contract that the launcher's engine registry (VK-1681) reads.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    struct ParsedVersion
    {
        uint32_t major = 0;
        uint32_t minor = 0;
        uint32_t patch = 0;
        std::string text;
    };

    // The rules of vfReadEngineVersion() in tools/engine_version.lua: a UTF-8 BOM and
    // surrounding whitespace are ignored; the rest must be MAJOR.MINOR.PATCH, decimal,
    // without leading zeros, each part fitting in 32 bits.
    std::optional<ParsedVersion> parseVersionText(std::string content)
    {
        if (content.compare(0, 3, "\xEF\xBB\xBF") == 0)
            content.erase(0, 3);

        auto isBlank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        size_t begin = 0;
        size_t end = content.size();
        while (begin < end && isBlank(content[begin]))
            ++begin;
        while (end > begin && isBlank(content[end - 1]))
            --end;

        ParsedVersion parsed;
        parsed.text = content.substr(begin, end - begin);
        const std::string& text = parsed.text;

        uint32_t* parts[3] = {&parsed.major, &parsed.minor, &parsed.patch};
        size_t pos = 0;
        for (int part = 0; part < 3; ++part)
        {
            const size_t start = pos;
            uint64_t value = 0;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
            {
                value = value * 10 + static_cast<uint64_t>(text[pos] - '0');
                if (value > 0xFFFFFFFFull)
                    return std::nullopt;
                ++pos;
            }
            const size_t digits = pos - start;
            if (digits == 0 || (digits > 1 && text[start] == '0'))
                return std::nullopt;
            *parts[part] = static_cast<uint32_t>(value);

            if (part < 2)
            {
                if (pos >= text.size() || text[pos] != '.')
                    return std::nullopt;
                ++pos;
            }
        }
        if (pos != text.size())
            return std::nullopt;
        return parsed;
    }

    // This Tests build's configuration, from the defines vfStandardConfigs() sets.
    constexpr const char* thisConfiguration()
    {
#if defined(DEBUG)
        return "Debug";
#elif defined(VF_DEVELOPMENT)
        return "Development";
#else
        return "Release";
#endif
    }

    std::string assetFormatVersionText()
    {
        return std::to_string(AssetFormatVersion::major) + "." + std::to_string(AssetFormatVersion::minor) + "." +
               std::to_string(AssetFormatVersion::patch);
    }

    std::string stringField(const json& object, const char* key)
    {
        const auto it = object.find(key);
        return (it != object.end() && it->is_string()) ? it->get<std::string>() : std::string{};
    }

    bool isLowerHex40(const std::string& text)
    {
        if (text.size() != 40)
            return false;
        for (char c : text)
        {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return false;
        }
        return true;
    }

    struct TempDir
    {
        fs::path path;

        explicit TempDir(const char* name) : path(fs::temp_directory_path() / name)
        {
            std::error_code ec;
            fs::remove_all(path, ec);
            fs::create_directories(path, ec);
        }

        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };
}

TEST_SUITE("EngineVersion")
{
    TEST_CASE("the VERSION parser follows the premake rules")
    {
        const auto plain = parseVersionText("1.0.0");
        REQUIRE(plain.has_value());
        CHECK(plain->major == 1u);
        CHECK(plain->minor == 0u);
        CHECK(plain->patch == 0u);
        CHECK(plain->text == "1.0.0");

        const auto decorated = parseVersionText("\xEF\xBB\xBF 2.10.3\r\n");
        REQUIRE(decorated.has_value());
        CHECK(decorated->major == 2u);
        CHECK(decorated->minor == 10u);
        CHECK(decorated->patch == 3u);
        CHECK(decorated->text == "2.10.3");

        CHECK(parseVersionText("4294967295.0.0").has_value());

        CHECK_FALSE(parseVersionText("").has_value());
        CHECK_FALSE(parseVersionText("1.0").has_value());
        CHECK_FALSE(parseVersionText("1.0.0.0").has_value());
        CHECK_FALSE(parseVersionText("01.0.0").has_value());
        CHECK_FALSE(parseVersionText("1.0.0-beta").has_value());
        CHECK_FALSE(parseVersionText("v1.0.0").has_value());
        CHECK_FALSE(parseVersionText("1. 0.0").has_value());
        CHECK_FALSE(parseVersionText("4294967296.0.0").has_value());
    }

    TEST_CASE("Version matches the repo-root VERSION file")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        const fs::path versionFile = *root / "VERSION";
        REQUIRE_MESSAGE(fs::exists(versionFile), "missing " << versionFile.string());

        const auto parsed = parseVersionText(repo_scan::readFile(versionFile));
        REQUIRE_MESSAGE(parsed.has_value(), "VERSION is not MAJOR.MINOR.PATCH");

        INFO("VERSION says " << parsed->text << " but this build compiled " << Version::text
                             << ": re-run premake5 vs2022 and rebuild");
        CHECK(Version::major == parsed->major);
        CHECK(Version::minor == parsed->minor);
        CHECK(Version::patch == parsed->patch);
        CHECK(std::string(Version::text) == parsed->text);
        CHECK(std::string(Version::displayName) == "VertexForge " + parsed->text);
    }

    TEST_CASE("Version::text and displayName spell Version's numbers")
    {
        const std::string numbers =
            std::to_string(Version::major) + "." + std::to_string(Version::minor) + "." + std::to_string(Version::patch);
        CHECK(std::string(Version::text) == numbers);
        CHECK(std::string(Version::displayName) == "VertexForge " + numbers);
    }

    TEST_CASE("a saved .vfproj carries the engine version from VERSION")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        const auto parsed = parseVersionText(repo_scan::readFile(*root / "VERSION"));
        REQUIRE(parsed.has_value());

        TempDir temp("VertexForge_EngineVersion_SaveProject");
        config::ProjectConfig project;
        project.projectName = "EngineVersionProject";
        project.version = "1.0";
        project.workingDirectory = temp.path.string();
        project.startupScene = "Startup.vfScene";
        project.engineVersion = "VertexForge 0.0.1"; // whatever the file said: a save rewrites it

        const fs::path projectFile = temp.path / "EngineVersion.vfproj";
        REQUIRE(serialization::ProjectSerialization::saveProject(project, projectFile.string()));

        std::ifstream file(projectFile);
        const json saved = json::parse(file, nullptr, false);
        REQUIRE_FALSE(saved.is_discarded());
        CHECK(stringField(saved, "engineVersion") == "VertexForge " + parsed->text);
        CHECK(stringField(saved, "engineVersion") == std::string(Version::displayName));
    }

    TEST_CASE("the dev engine.json beside this configuration's Editor.exe")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");

        const fs::path manifestDir = *root / "bin" / "Editor" / thisConfiguration() / "x64";
        const fs::path manifestPath = manifestDir / "engine.json";
        if (!fs::exists(manifestPath))
        {
            MESSAGE("skipped: " << manifestPath.string() << " not found (build the Editor in " << thisConfiguration()
                                << ": its postbuild writes the manifest)");
            return;
        }

        const json manifest = json::parse(repo_scan::readFile(manifestPath), nullptr, false);
        REQUIRE_MESSAGE(!manifest.is_discarded(), manifestPath.string() << " is not valid JSON");
        REQUIRE(manifest.is_object());
        INFO("manifest " << manifestPath.string() << " (rebuild the Editor " << thisConfiguration()
                         << " if it is stale)");

        // The VK-1681 contract: exactly these fields.
        const char* keys[] = {"manifestVersion", "engineVersion", "assetFormatVersion", "projectSchemaVersion",
                              "pluginApiVersion", "configuration", "gitCommit", "editorExe",
                              "workingDirectory", "templatesDir", "scriptLibDir"};
        CHECK(manifest.size() == std::size(keys));
        for (const char* key : keys)
        {
            CAPTURE(key);
            CHECK(manifest.contains(key));
        }

        REQUIRE(manifest.contains("manifestVersion"));
        CHECK(manifest["manifestVersion"].is_number_unsigned());
        CHECK(manifest["manifestVersion"] == 1);

        CHECK(stringField(manifest, "engineVersion") == std::string(Version::text));
        CHECK(stringField(manifest, "assetFormatVersion") == assetFormatVersionText());
        CHECK(stringField(manifest, "projectSchemaVersion") == config::ProjectSchemaVersion::toString());
        REQUIRE(manifest.contains("pluginApiVersion"));
        REQUIRE(manifest["pluginApiVersion"].is_number_unsigned());
        CHECK(manifest["pluginApiVersion"].get<uint32_t>() == plugin::VF_PLUGIN_API_VERSION);
        CHECK(stringField(manifest, "configuration") == std::string(thisConfiguration()));

        REQUIRE(manifest.contains("gitCommit"));
        const json& gitCommit = manifest["gitCommit"];
        CHECK((gitCommit.is_null() || (gitCommit.is_string() && isLowerHex40(gitCommit.get<std::string>()))));

        const std::string editorExe = stringField(manifest, "editorExe");
        CHECK(editorExe == "Editor.exe");
        CHECK(fs::exists(manifestDir / editorExe));

        // Paths are relative to the manifest, with forward slashes.
        for (const char* key : {"workingDirectory", "templatesDir", "scriptLibDir"})
        {
            CAPTURE(key);
            const std::string value = stringField(manifest, key);
            CHECK_FALSE(value.empty());
            CHECK(value.find('\\') == std::string::npos);
            CHECK(fs::path(value).is_relative());
        }

        std::error_code ec;
        const fs::path workingDirectory = fs::weakly_canonical(manifestDir / stringField(manifest, "workingDirectory"), ec);
        CHECK(fs::equivalent(workingDirectory, *root / "VFEngine" / "editor", ec));
        // The editor loads engine resources from "../../resources/" relative to its working directory.
        CHECK(fs::exists(workingDirectory / ".." / ".." / "resources" / "shaders"));

        const fs::path scriptLibDir = fs::weakly_canonical(manifestDir / stringField(manifest, "scriptLibDir"), ec);
        CHECK(fs::equivalent(scriptLibDir, *root / "assets" / "scripts" / "lib", ec));

        // The templates folder arrives with VK-1689; compare the resolved path, not the folder.
        const fs::path templatesDir = fs::weakly_canonical(manifestDir / stringField(manifest, "templatesDir"), ec);
        CHECK(templatesDir == fs::weakly_canonical(*root / "assets" / "templates", ec));
    }
}
