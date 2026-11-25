#include <doctest.h>

#include "events/EventDispatcher.hpp"
#include "events/project/ProjectEvents.hpp"
#include "impl/project/ProjectServiceImpl.hpp"
#include "serialization/ProjectSerialization.hpp"
#include "config/Config.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    config::ProjectConfig makeValidProjectConfig(const std::filesystem::path& workingDirectory)
    {
        config::ProjectConfig config;
        config.projectName = "ProjectServiceTest";
        config.version = "1.0";
        config.workingDirectory = workingDirectory.string();
        config.startupScene = "Startup.vfScene";
        return config;
    }

    std::filesystem::path makeTestDirectory(const char* name)
    {
        auto directory = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        return directory;
    }

    struct DispatcherScope
    {
        DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }

        ~DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }
    };

    // VK-1687 helpers.
    std::string readText(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // The "workingDirectory" string exactly as the .vfproj on disk holds it.
    std::string savedWorkingDirectory(const std::filesystem::path& projectFile)
    {
        return nlohmann::json::parse(readText(projectFile)).at("workingDirectory").get<std::string>();
    }

    // Writes a .vfproj whose workingDirectory is exactly `workingDirectory`.
    void writeProject(const std::filesystem::path& projectFile, const std::string& workingDirectory)
    {
        config::ProjectConfig config = makeValidProjectConfig({});
        config.workingDirectory = workingDirectory;
        REQUIRE(serialization::ProjectSerialization::saveProject(config, projectFile.string()));
    }

    // What Project Settings > Save does (ProjectSettingsWindow::saveToProject): send back the config
    // it read, then save in place.
    void saveLikeProjectSettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        const auto project = dispatcher.query(events::project::GetCurrentProjectQuery{});
        REQUIRE(project.has_value());

        events::project::UpdateProjectConfigCommand update;
        update.config = *project;
        REQUIRE(dispatcher.execute(update));

        events::project::SaveProjectCommand save;
        save.filePath = "";
        REQUIRE(dispatcher.execute(save));
    }

    // VK-1690 helpers.
    constexpr const char* olderEngineVersion = "VertexForge 0.9.0";

    // A .vfproj as a hand edit or another engine left it (LF, final line break): members in
    // authored order, members the engine does not read (a nested object in its own order, numbers)
    // and an older engineVersion; no assetFormatVersion.
    std::string foreignProject(bool withEngineVersion)
    {
        std::string text = "{\n"
                           "  \"schemaVersion\": \"1.0\",\n"
                           "  \"projectName\": \"StampTest\",\n"
                           "  \"custom\": {\"inner\": {\"b\": 2, \"a\": 1}, \"list\": [1, 2.5, \"x\"], \"flag\": true},\n";
        if (withEngineVersion)
        {
            text += "  \"engineVersion\": \"VertexForge 0.9.0\",\n";
        }
        text += "  \"version\": \"1.0\",\n"
                "  \"workingDirectory\": \".\",\n"
                "  \"startupScene\": \"Startup.vfScene\",\n"
                "  \"pluginApiVersion\": 7,\n"
                "  \"ratio\": 0.25\n"
                "}\n";
        return text;
    }

    void writeBytes(const std::filesystem::path& file, const std::string& bytes)
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        REQUIRE_FALSE(out.fail());
    }

    std::string toCrlf(const std::string& text)
    {
        std::string result;
        for (const char c : text)
        {
            if (c == '\n')
            {
                result += '\r';
            }
            result += c;
        }
        return result;
    }

    size_t countOf(const std::string& text, const std::string& needle)
    {
        size_t count = 0;
        for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size()))
        {
            ++count;
        }
        return count;
    }

    serialization::ProjectStamp stampFile(const std::filesystem::path& projectFile)
    {
        return serialization::ProjectSerialization::stampEngineVersions(projectFile.string());
    }

    bool stampTempExists(const std::filesystem::path& projectFile)
    {
        std::filesystem::path temp = projectFile;
        temp += ".stamp.tmp";
        std::error_code ec;
        return std::filesystem::exists(temp, ec);
    }

    long long writeTimeOf(const std::filesystem::path& file)
    {
        return static_cast<long long>(std::filesystem::last_write_time(file).time_since_epoch().count());
    }

    // The top-level member names, in file order.
    std::vector<std::string> memberOrder(const std::string& text)
    {
        const auto document = nlohmann::ordered_json::parse(text);
        std::vector<std::string> keys;
        for (auto it = document.begin(); it != document.end(); ++it)
        {
            keys.push_back(it.key());
        }
        return keys;
    }

    void checkStampedMembers(const std::string& text)
    {
        const auto document = nlohmann::ordered_json::parse(text);
        REQUIRE(document.contains("engineVersion"));
        REQUIRE(document.at("engineVersion").is_string());
        CHECK(document.at("engineVersion").get<std::string>() == Version::displayName);
        REQUIRE(document.contains("assetFormatVersion"));
        REQUIRE(document.at("assetFormatVersion").is_string());
        CHECK(document.at("assetFormatVersion").get<std::string>() == AssetFormatVersion::toString());
    }

    // Every member but the two the stamp owns keeps its value (nested member order included).
    void checkOtherMembersKept(const std::string& before, const std::string& after)
    {
        const auto original = nlohmann::ordered_json::parse(before);
        const auto stamped = nlohmann::ordered_json::parse(after);
        for (auto it = original.begin(); it != original.end(); ++it)
        {
            const std::string& key = it.key();
            if (key == "engineVersion" || key == "assetFormatVersion")
            {
                continue;
            }
            CAPTURE(key);
            REQUIRE(stamped.contains(key));
            CHECK(stamped.at(key) == it.value());
        }
    }

    // Read-only for its lifetime. MSVC sets FILE_ATTRIBUTE_READONLY only when ALL write bits are
    // removed; adding any write bit clears it again.
    struct ReadOnlyScope
    {
        std::filesystem::path file;

        explicit ReadOnlyScope(std::filesystem::path path) : file(std::move(path))
        {
            namespace fs = std::filesystem;
            fs::permissions(file, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                            fs::perm_options::remove);
            REQUIRE((fs::status(file).permissions() & fs::perms::owner_write) == fs::perms::none);
        }

        ~ReadOnlyScope()
        {
            std::error_code ec;
            std::filesystem::permissions(file, std::filesystem::perms::owner_write, std::filesystem::perm_options::add,
                                         ec);
        }

        ReadOnlyScope(const ReadOnlyScope&) = delete;
        ReadOnlyScope& operator=(const ReadOnlyScope&) = delete;
    };
}

TEST_SUITE("ProjectService")
{
    TEST_CASE("SaveProjectCommand with empty path saves to current project path")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_SaveInPlace");
        const auto projectPath = directory / "ProjectServiceTest.vfproject";

        auto config = makeValidProjectConfig(directory);
        REQUIRE(serialization::ProjectSerialization::saveProject(config, projectPath.string()));

        services::ProjectServiceImpl service;
        service.registerEventHandlers();
        REQUIRE(service.loadProject(projectPath.string()));

        auto updatedConfig = *service.getCurrentProject();
        updatedConfig.startupScene = "UpdatedStartup.vfScene";

        events::project::UpdateProjectConfigCommand updateCommand;
        updateCommand.config = updatedConfig;
        REQUIRE(events::EventDispatcher::instance().execute(updateCommand));

        events::project::SaveProjectCommand saveCommand;
        saveCommand.filePath = "";
        CHECK(events::EventDispatcher::instance().execute(saveCommand));

        auto pathAfterSave = events::EventDispatcher::instance().query(events::project::GetProjectPathQuery{});
        REQUIRE(pathAfterSave.has_value());
        CHECK(*pathAfterSave == projectPath.string());

        auto savedProject = serialization::ProjectSerialization::loadProject(projectPath.string());
        REQUIRE(savedProject.has_value());
        CHECK(savedProject->startupScene == "UpdatedStartup.vfScene");

        std::filesystem::remove_all(directory);
    }

    TEST_CASE("SaveProjectCommand with empty path fails for unsaved project")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_Unsaved");

        services::ProjectServiceImpl service;
        service.registerEventHandlers();
        REQUIRE(service.newProject(makeValidProjectConfig(directory)));

        events::project::SaveProjectCommand saveCommand;
        saveCommand.filePath = "";
        CHECK_FALSE(events::EventDispatcher::instance().execute(saveCommand));
        CHECK_FALSE(service.getProjectPath().has_value());

        std::filesystem::remove_all(directory);
    }

    TEST_CASE("successful project config updates publish the updated project")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_ConfigUpdated");

        services::ProjectServiceImpl service;
        auto projectConfig = makeValidProjectConfig(directory);

        int notificationCount = 0;
        config::ProjectConfig notifiedProject;
        auto token = events::EventDispatcher::instance().subscribe<
            events::project::ProjectConfigUpdatedNotification>(
            [&](const events::project::ProjectConfigUpdatedNotification& notification)
            {
                ++notificationCount;
                notifiedProject = notification.project;
            });

        auto invalid = projectConfig;
        invalid.projectName.clear();
        CHECK_FALSE(service.updateProjectConfig(invalid));
        CHECK(notificationCount == 0);

        projectConfig.fontFallbackChain = {"Assets/Fonts/Primary.vfFont",
                                           "Assets/Fonts/Emoji.vfFont"};
        REQUIRE(service.updateProjectConfig(projectConfig));
        CHECK(notificationCount == 1);
        CHECK(notifiedProject.fontFallbackChain == projectConfig.fontFallbackChain);

        events::EventDispatcher::instance().unsubscribe(token);
        std::filesystem::remove_all(directory);
    }

    // VK-1682: the editor's open guard moves its project lock; a refusal must leave the open project alone.
    TEST_CASE("a refusing open guard leaves the current project and publishes nothing")
    {
        DispatcherScope dispatcherScope;
        const auto directoryA = makeTestDirectory("VertexForge_ProjectService_GuardA");
        const auto directoryB = makeTestDirectory("VertexForge_ProjectService_GuardB");
        const auto projectA = directoryA / "A.vfproj";
        const auto projectB = directoryB / "B.vfproj";
        REQUIRE(serialization::ProjectSerialization::saveProject(makeValidProjectConfig(directoryA), projectA.string()));
        auto configB = makeValidProjectConfig(directoryB);
        configB.projectName = "GuardB";
        REQUIRE(serialization::ProjectSerialization::saveProject(configB, projectB.string()));

        services::ProjectServiceImpl service;
        service.registerEventHandlers();

        std::vector<std::filesystem::path> asked;
        service.setOpenGuard([&asked, projectB](const std::filesystem::path& projectFile) -> std::optional<std::string>
        {
            asked.push_back(projectFile);
            if (projectFile == projectB)
            {
                return std::string("another editor has it open");
            }
            return std::nullopt;
        });

        REQUIRE(service.loadProject(projectA.string()));
        REQUIRE(asked.size() == 1);
        CHECK(asked[0] == projectA);

        int loadedNotifications = 0;
        auto token = events::EventDispatcher::instance().subscribe<events::project::ProjectLoadedNotification>(
            [&loadedNotifications](const events::project::ProjectLoadedNotification&)
            {
                ++loadedNotifications;
            });

        CHECK_FALSE(service.loadProject(projectB.string()));
        CHECK(asked.size() == 2);
        CHECK(loadedNotifications == 0);
        REQUIRE(service.getCurrentProject().has_value());
        CHECK(service.getCurrentProject()->projectName == "ProjectServiceTest");
        REQUIRE(service.getProjectPath().has_value());
        CHECK(*service.getProjectPath() == projectA.string());

        events::EventDispatcher::instance().unsubscribe(token);
        std::filesystem::remove_all(directoryA);
        std::filesystem::remove_all(directoryB);
    }

    TEST_CASE("the open guard is not asked about a project that fails to parse")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_GuardParse");
        const auto broken = directory / "Broken.vfproj";
        {
            std::ofstream out(broken, std::ios::binary);
            out << "{ not json";
        }

        services::ProjectServiceImpl service;
        int guardCalls = 0;
        service.setOpenGuard([&guardCalls](const std::filesystem::path&) -> std::optional<std::string>
        {
            ++guardCalls;
            return std::nullopt;
        });

        CHECK_FALSE(service.loadProject(broken.string()));
        CHECK(guardCalls == 0);
        CHECK_FALSE(service.isProjectLoaded());

        std::filesystem::remove_all(directory);
    }

    // VK-1687: the .vfproj keeps the authored working directory; memory holds the resolved one.
    TEST_CASE("a relative working directory survives Project Settings Save and a folder move")
    {
        namespace fs = std::filesystem;
        DispatcherScope dispatcherScope;
        const auto root = makeTestDirectory("VertexForge_ProjectService_Portable");
        const auto original = root / "VK1687OriginalFolder";
        const auto moved = root / "VK1687MovedFolder";
        fs::create_directories(original / "scenes");
        {
            std::ofstream scene(original / "scenes" / "Main.vfScene", std::ios::binary);
            scene << "{}";
        }
        writeProject(original / "Game.vfproj", ".");

        services::ProjectServiceImpl service;
        service.registerEventHandlers();
        REQUIRE(service.loadProject((original / "Game.vfproj").string()));

        std::error_code ec;
        const std::string resolved = service.getCurrentProject()->workingDirectory;
        CHECK(fs::path(resolved).is_absolute());
        CHECK(fs::equivalent(resolved, original, ec));

        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(original / "Game.vfproj") == ".");
        CHECK(readText(original / "Game.vfproj").find("VK1687OriginalFolder") == std::string::npos);
        // Memory still holds the resolved path every reader joins onto.
        CHECK(service.getCurrentProject()->workingDirectory == resolved);

        fs::rename(original, moved);
        REQUIRE(service.loadProject((moved / "Game.vfproj").string()));
        const auto reopened = *service.getCurrentProject();
        CHECK(fs::equivalent(reopened.workingDirectory, moved, ec));
        CHECK(fs::exists(fs::path(reopened.workingDirectory) / "scenes" / "Main.vfScene"));

        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(moved / "Game.vfproj") == ".");

        fs::remove_all(root, ec);
    }

    TEST_CASE("an absolute working directory inside the project is saved relative")
    {
        namespace fs = std::filesystem;
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_Repair");
        fs::create_directories(directory / "Assets");

        services::ProjectServiceImpl service;
        service.registerEventHandlers();

        // What an older editor wrote after one Project Settings Save: its own folder, absolute.
        writeProject(directory / "Self.vfproj", directory.string());
        REQUIRE(service.loadProject((directory / "Self.vfproj").string()));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Self.vfproj") == ".");

        writeProject(directory / "Assets.vfproj", (directory / "Assets").string());
        REQUIRE(service.loadProject((directory / "Assets.vfproj").string()));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Assets.vfproj") == "Assets");

        // A folder that does not exist, written with a trailing separator, is never saved as "".
        const std::string missing =
            (directory / "Missing").string() + std::string(1, static_cast<char>(fs::path::preferred_separator));
        writeProject(directory / "Missing.vfproj", missing);
        REQUIRE(service.loadProject((directory / "Missing.vfproj").string()));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Missing.vfproj") == "Missing");

        std::error_code ec;
        fs::remove_all(directory, ec);
    }

    TEST_CASE("working directories outside the project are kept exactly as written")
    {
        namespace fs = std::filesystem;
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_Outside");
        const auto shared = makeTestDirectory("VertexForge_ProjectService_OutsideShared");

        services::ProjectServiceImpl service;
        service.registerEventHandlers();

        // Forward slashes on purpose: the authored spelling, not a normalised one, comes back.
        const std::string absoluteOutside = shared.generic_string();
        writeProject(directory / "Absolute.vfproj", absoluteOutside);
        REQUIRE(service.loadProject((directory / "Absolute.vfproj").string()));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Absolute.vfproj") == absoluteOutside);

        const std::string relativeOutside = "../VertexForge_ProjectService_OutsideShared";
        writeProject(directory / "Relative.vfproj", relativeOutside);
        REQUIRE(service.loadProject((directory / "Relative.vfproj").string()));
        std::error_code ec;
        CHECK(fs::equivalent(service.getCurrentProject()->workingDirectory, shared, ec));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Relative.vfproj") == relativeOutside);

        fs::remove_all(directory, ec);
        fs::remove_all(shared, ec);
    }

    TEST_CASE("the authored working directory follows updates, Save As and refused switches")
    {
        namespace fs = std::filesystem;
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_Authored");
        const auto other = makeTestDirectory("VertexForge_ProjectService_AuthoredOther");
        fs::create_directories(directory / "Assets");
        fs::create_directories(directory / "Content");
        std::error_code ec;

        services::ProjectServiceImpl service;
        service.registerEventHandlers();
        auto& dispatcher = events::EventDispatcher::instance();

        // A spelling only rule 1 (written verbatim) keeps; anything recomputed would give "Assets".
        writeProject(directory / "Game.vfproj", "./Assets");
        writeProject(other / "Other.vfproj", "Elsewhere");
        service.setOpenGuard([other](const fs::path& projectFile) -> std::optional<std::string>
        {
            if (projectFile.parent_path() == other)
            {
                return std::string("refused");
            }
            return std::nullopt;
        });

        REQUIRE(service.loadProject((directory / "Game.vfproj").string()));
        CHECK_FALSE(service.loadProject((other / "Other.vfproj").string()));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Game.vfproj") == "./Assets");

        // A new relative value is resolved against the project folder and written as given.
        auto updated = *service.getCurrentProject();
        updated.workingDirectory = "Content";
        events::project::UpdateProjectConfigCommand update;
        update.config = updated;
        REQUIRE(dispatcher.execute(update));
        CHECK(fs::path(service.getCurrentProject()->workingDirectory).is_absolute());
        CHECK(fs::equivalent(service.getCurrentProject()->workingDirectory, directory / "Content", ec));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Game.vfproj") == "Content");

        // Save As into a folder that does not contain the working directory: it must still name it.
        events::project::SaveProjectCommand saveAs;
        saveAs.filePath = (other / "Copy.vfproj").string();
        REQUIRE(dispatcher.execute(saveAs));
        const std::string copied = savedWorkingDirectory(other / "Copy.vfproj");
        CHECK(fs::path(copied).is_absolute());
        CHECK(fs::equivalent(copied, directory / "Content", ec));

        fs::remove_all(directory, ec);
        fs::remove_all(other, ec);
    }

    // VK-1690: the editor records in the .vfproj that it opened the project; nothing else may change.
    TEST_CASE("the engine stamp rewrites only engineVersion and assetFormatVersion")
    {
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampInPlace");
        const auto projectFile = directory / "Game.vfproj";

        // An older engine's value is replaced where it is; the missing asset format goes right after it.
        const std::string original = foreignProject(true);
        writeBytes(projectFile, original);
        const serialization::ProjectStamp result = stampFile(projectFile);
        CHECK(result.status == serialization::ProjectStampStatus::Stamped);
        CHECK(result.message.empty());
        const std::string stamped = readText(projectFile);
        REQUIRE_FALSE(stamped.empty());
        checkStampedMembers(stamped);
        checkOtherMembersKept(original, stamped);
        const std::vector<std::string> insertedOrder = {"schemaVersion",    "projectName",      "custom",
                                                        "engineVersion",    "assetFormatVersion", "version",
                                                        "workingDirectory", "startupScene",     "pluginApiVersion",
                                                        "ratio"};
        CHECK(memberOrder(stamped) == insertedOrder);
        CHECK(stamped.find('\r') == std::string::npos);
        CHECK(stamped.back() == '\n');
        CHECK_FALSE(stampTempExists(projectFile));

        // Both present with other values, the asset format first: each is replaced where it is, and a
        // file without a final line break gets none.
        const std::string reordered = "{\n"
                                      "  \"assetFormatVersion\": \"0.9.0\",\n"
                                      "  \"schemaVersion\": \"1.0\",\n"
                                      "  \"engineVersion\": \"VertexForge 0.9.0\",\n"
                                      "  \"projectName\": \"StampTest\"\n"
                                      "}";
        writeBytes(projectFile, reordered);
        CHECK(stampFile(projectFile).status == serialization::ProjectStampStatus::Stamped);
        const std::string restamped = readText(projectFile);
        REQUIRE_FALSE(restamped.empty());
        checkStampedMembers(restamped);
        checkOtherMembersKept(reordered, restamped);
        const std::vector<std::string> sameOrder = {"assetFormatVersion", "schemaVersion", "engineVersion",
                                                    "projectName"};
        CHECK(memberOrder(restamped) == sameOrder);
        CHECK(restamped.back() == '}');
        CHECK_FALSE(stampTempExists(projectFile));

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("the engine stamp appends both members when engineVersion is missing")
    {
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampAppend");
        const auto projectFile = directory / "Game.vfproj";
        const std::string original = foreignProject(false);
        writeBytes(projectFile, original);

        CHECK(stampFile(projectFile).status == serialization::ProjectStampStatus::Stamped);
        const std::string stamped = readText(projectFile);
        checkStampedMembers(stamped);
        checkOtherMembersKept(original, stamped);
        const std::vector<std::string> appendedOrder = {"schemaVersion",    "projectName",  "custom",
                                                        "version",          "workingDirectory", "startupScene",
                                                        "pluginApiVersion", "ratio",        "engineVersion",
                                                        "assetFormatVersion"};
        CHECK(memberOrder(stamped) == appendedOrder);
        CHECK_FALSE(stampTempExists(projectFile));

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("a second engine stamp writes nothing")
    {
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampTwice");
        const auto projectFile = directory / "Game.vfproj";
        writeBytes(projectFile, foreignProject(true));
        REQUIRE(stampFile(projectFile).status == serialization::ProjectStampStatus::Stamped);

        const std::string bytes = readText(projectFile);
        const long long writeTime = writeTimeOf(projectFile);
        const serialization::ProjectStamp again = stampFile(projectFile);
        CHECK(again.status == serialization::ProjectStampStatus::Unchanged);
        CHECK(again.message.empty());
        CHECK(readText(projectFile) == bytes);
        CHECK(writeTimeOf(projectFile) == writeTime);
        CHECK_FALSE(stampTempExists(projectFile));

        // What saveProject writes already names this engine.
        const auto saved = directory / "Saved.vfproj";
        REQUIRE(serialization::ProjectSerialization::saveProject(makeValidProjectConfig(directory), saved.string()));
        const std::string savedBytes = readText(saved);
        CHECK(stampFile(saved).status == serialization::ProjectStampStatus::Unchanged);
        CHECK(readText(saved) == savedBytes);

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("the engine stamp keeps the line endings and a UTF-8 BOM")
    {
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampLineEndings");

        const auto crlfFile = directory / "Crlf.vfproj";
        const std::string crlfOriginal = toCrlf(foreignProject(true));
        writeBytes(crlfFile, crlfOriginal);
        REQUIRE(stampFile(crlfFile).status == serialization::ProjectStampStatus::Stamped);
        const std::string crlf = readText(crlfFile);
        checkStampedMembers(crlf);
        checkOtherMembersKept(crlfOriginal, crlf);
        CHECK(countOf(crlf, "\r\n") > 0);
        CHECK(countOf(crlf, "\r\n") == countOf(crlf, "\n"));
        CHECK(countOf(crlf, "\r\n") == countOf(crlf, "\r"));
        CHECK(crlf.ends_with("}\r\n"));

        const auto lfFile = directory / "Lf.vfproj";
        const std::string lfOriginal = foreignProject(true);
        writeBytes(lfFile, lfOriginal);
        REQUIRE(stampFile(lfFile).status == serialization::ProjectStampStatus::Stamped);
        const std::string lf = readText(lfFile);
        checkStampedMembers(lf);
        CHECK(countOf(lf, "\n") > 0);
        CHECK(lf.find('\r') == std::string::npos);
        CHECK(lf.ends_with("}\n"));

        const auto bomFile = directory / "Bom.vfproj";
        const std::string bomOriginal = "\xEF\xBB\xBF" + toCrlf(foreignProject(true));
        writeBytes(bomFile, bomOriginal);
        REQUIRE(stampFile(bomFile).status == serialization::ProjectStampStatus::Stamped);
        const std::string bom = readText(bomFile);
        CHECK(bom.starts_with("\xEF\xBB\xBF{\r\n"));
        CHECK(countOf(bom, "\xEF\xBB\xBF") == 1);
        checkStampedMembers(bom);
        checkOtherMembersKept(bomOriginal, bom);

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("the engine stamp leaves a file it cannot parse untouched")
    {
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampInvalid");
        const auto projectFile = directory / "Broken.vfproj";
        for (const std::string& content : {std::string("{ not json"), std::string("[1, 2]"), std::string()})
        {
            CAPTURE(content);
            writeBytes(projectFile, content);
            const serialization::ProjectStamp result = stampFile(projectFile);
            CHECK(result.status == serialization::ProjectStampStatus::Failed);
            CHECK_FALSE(result.message.empty());
            CHECK(readText(projectFile) == content);
            CHECK_FALSE(stampTempExists(projectFile));
        }

        const auto missing = directory / "Missing.vfproj";
        CHECK(stampFile(missing).status == serialization::ProjectStampStatus::Failed);
        CHECK_FALSE(std::filesystem::exists(missing));
        CHECK_FALSE(stampTempExists(missing));

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("the engine stamp leaves a read-only file untouched")
    {
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampReadOnly");
        const auto projectFile = directory / "Game.vfproj";
        const std::string original = foreignProject(true);
        writeBytes(projectFile, original);
        {
            ReadOnlyScope readOnly(projectFile);
            const serialization::ProjectStamp result = stampFile(projectFile);
            CHECK(result.status == serialization::ProjectStampStatus::Failed);
            CHECK_FALSE(result.message.empty());
            CHECK(readText(projectFile) == original);
            CHECK_FALSE(stampTempExists(projectFile));
        }

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("the project service stamps the opened project only when enabled")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampOnOpen");
        const auto projectFile = directory / "Game.vfproj";
        const std::string original = foreignProject(true);
        writeBytes(projectFile, original);

        {
            // The default, which the Runtime keeps: the file is never written.
            services::ProjectServiceImpl service;
            REQUIRE(service.loadProject(projectFile.string()));
            CHECK(readText(projectFile) == original);
            REQUIRE(service.getCurrentProject().has_value());
            REQUIRE(service.getCurrentProject()->engineVersion.has_value());
            CHECK(*service.getCurrentProject()->engineVersion == olderEngineVersion);
        }

        std::optional<std::string> notifiedEngineVersion;
        auto token = events::EventDispatcher::instance().subscribe<events::project::ProjectLoadedNotification>(
            [&notifiedEngineVersion](const events::project::ProjectLoadedNotification& notification)
            {
                notifiedEngineVersion = notification.project.engineVersion;
            });

        services::ProjectServiceImpl editorService;
        editorService.setStampEngineVersionOnOpen(true);
        REQUIRE(editorService.loadProject(projectFile.string()));
        const std::string stamped = readText(projectFile);
        checkStampedMembers(stamped);
        checkOtherMembersKept(original, stamped);
        CHECK_FALSE(stampTempExists(projectFile));
        REQUIRE(editorService.getCurrentProject().has_value());
        REQUIRE(editorService.getCurrentProject()->engineVersion.has_value());
        CHECK(*editorService.getCurrentProject()->engineVersion == Version::displayName);
        REQUIRE(notifiedEngineVersion.has_value());
        CHECK(*notifiedEngineVersion == Version::displayName);

        // Opening it again writes nothing.
        const long long writeTime = writeTimeOf(projectFile);
        REQUIRE(editorService.loadProject(projectFile.string()));
        CHECK(readText(projectFile) == stamped);
        CHECK(writeTimeOf(projectFile) == writeTime);

        events::EventDispatcher::instance().unsubscribe(token);
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("a refused open guard never stamps the project")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampRefused");
        const auto projectFile = directory / "Game.vfproj";
        const std::string original = foreignProject(true);
        writeBytes(projectFile, original);

        services::ProjectServiceImpl service;
        service.setStampEngineVersionOnOpen(true);
        service.setOpenGuard([](const std::filesystem::path&) -> std::optional<std::string>
        {
            return std::string("another editor has it open");
        });

        CHECK_FALSE(service.loadProject(projectFile.string()));
        CHECK_FALSE(service.isProjectLoaded());
        CHECK(readText(projectFile) == original);
        CHECK_FALSE(stampTempExists(projectFile));

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("the engine stamp never lowers a newer engine's record")
    {
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampNeverLowers");
        const auto projectFile = directory / "Game.vfproj";
        const std::string newerEngine = "VertexForge 999.0.0";
        const std::string newerFormat = "999.0.0";

        // Both newer: nothing is written, and the result says what the file keeps recording.
        const std::string bothNewer = "{\n"
                                      "  \"schemaVersion\": \"1.0\",\n"
                                      "  \"projectName\": \"StampTest\",\n"
                                      "  \"engineVersion\": \"" + newerEngine + "\",\n"
                                      "  \"assetFormatVersion\": \"" + newerFormat + "\"\n"
                                      "}\n";
        writeBytes(projectFile, bothNewer);
        const long long writeTime = writeTimeOf(projectFile);
        const serialization::ProjectStamp kept = stampFile(projectFile);
        CHECK(kept.status == serialization::ProjectStampStatus::Unchanged);
        CHECK(kept.engineVersion == newerEngine);
        CHECK(kept.assetFormatVersion == newerFormat);
        CHECK(readText(projectFile) == bothNewer);
        CHECK(writeTimeOf(projectFile) == writeTime);

        // Only one newer: that one stays, the other is raised to this engine's.
        const std::string engineNewer = "{\n"
                                        "  \"projectName\": \"StampTest\",\n"
                                        "  \"engineVersion\": \"" + newerEngine + "\",\n"
                                        "  \"assetFormatVersion\": \"0.9.0\"\n"
                                        "}\n";
        writeBytes(projectFile, engineNewer);
        const serialization::ProjectStamp raised = stampFile(projectFile);
        CHECK(raised.status == serialization::ProjectStampStatus::Stamped);
        CHECK(raised.engineVersion == newerEngine);
        CHECK(raised.assetFormatVersion == AssetFormatVersion::toString());
        const auto document = nlohmann::ordered_json::parse(readText(projectFile));
        CHECK(document.at("engineVersion").get<std::string>() == newerEngine);
        CHECK(document.at("assetFormatVersion").get<std::string>() == AssetFormatVersion::toString());

        // A part too large for 64 bits still reads as a newer version.
        const std::string huge = "VertexForge 99999999999999999999999.0.0";
        writeBytes(projectFile, "{\"projectName\": \"StampTest\", \"engineVersion\": \"" + huge
                                    + "\", \"assetFormatVersion\": \"" + AssetFormatVersion::toString() + "\"}");
        CHECK(stampFile(projectFile).status == serialization::ProjectStampStatus::Unchanged);
        CHECK_FALSE(stampTempExists(projectFile));

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("the engine stamp replaces a record that is not a version")
    {
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampNotAVersion");
        const auto projectFile = directory / "Game.vfproj";
        // Not MAJOR.MINOR.PATCH (a leading zero, two or four parts, a word, a number), or white space the
        // launcher's VersionNumber does not trim either (a vertical tab): taken as older.
        for (const std::string& stored :
             {std::string("\"VertexForge 01.0.0\""), std::string("\"1.0\""), std::string("\"999.0.0.1\""),
              std::string("\"VertexForge two\""), std::string("7"), std::string("\"VertexForge 999.0.0\\u000b\"")})
        {
            CAPTURE(stored);
            writeBytes(projectFile, "{\"projectName\": \"StampTest\", \"engineVersion\": " + stored
                                        + ", \"assetFormatVersion\": " + stored + "}");
            CHECK(stampFile(projectFile).status == serialization::ProjectStampStatus::Stamped);
            checkStampedMembers(readText(projectFile));
        }

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    // The launcher's VersionNumber twin: parseProduct trims spaces, tabs, CRs and LFs around an
    // engineVersion; parse takes an assetFormatVersion exactly as written.
    TEST_CASE("the engine stamp reads versions as the launcher reads them")
    {
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampLikeLauncher");
        const auto projectFile = directory / "Game.vfproj";

        // A newer engineVersion with blanks around it is still newer: kept as written.
        const std::string padded = "{\"projectName\": \"StampTest\", \"engineVersion\": \" VertexForge 999.0.0\\t\","
                                   " \"assetFormatVersion\": \"" + AssetFormatVersion::toString() + "\"}";
        writeBytes(projectFile, padded);
        const serialization::ProjectStamp kept = stampFile(projectFile);
        CHECK(kept.status == serialization::ProjectStampStatus::Unchanged);
        CHECK(kept.engineVersion == " VertexForge 999.0.0\t");
        CHECK(readText(projectFile) == padded);

        // An assetFormatVersion with a blank is not a version (the launcher calls it malformed): replaced.
        writeBytes(projectFile, "{\"projectName\": \"StampTest\", \"engineVersion\": \"" + std::string(Version::displayName)
                                    + "\", \"assetFormatVersion\": \" 999.0.0\"}");
        const serialization::ProjectStamp replaced = stampFile(projectFile);
        CHECK(replaced.status == serialization::ProjectStampStatus::Stamped);
        CHECK(replaced.assetFormatVersion == AssetFormatVersion::toString());
        checkStampedMembers(readText(projectFile));

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("saving a project never lowers a newer engine's record")
    {
        const auto directory = makeTestDirectory("VertexForge_ProjectService_SaveNeverLowers");
        const auto projectFile = directory / "Game.vfproj";

        // A newer engine's values, as the open stamp kept them: in the config, and in the file it replaces.
        config::ProjectConfig project = makeValidProjectConfig(directory);
        project.engineVersion = "VertexForge 999.0.0";
        writeBytes(projectFile, "{\"assetFormatVersion\": \"999.0.0\"}");
        REQUIRE(serialization::ProjectSerialization::saveProject(project, projectFile.string()));
        const auto kept = nlohmann::json::parse(readText(projectFile));
        CHECK(kept.at("engineVersion").get<std::string>() == "VertexForge 999.0.0");
        CHECK(kept.at("assetFormatVersion").get<std::string>() == "999.0.0");

        // Older values, or none, are raised to this engine's.
        project.engineVersion = "VertexForge 0.9.0";
        writeBytes(projectFile, "{\"assetFormatVersion\": \"0.9.0\"}");
        REQUIRE(serialization::ProjectSerialization::saveProject(project, projectFile.string()));
        checkStampedMembers(readText(projectFile));
        const auto fresh = directory / "Fresh.vfproj";
        project.engineVersion.reset();
        REQUIRE(serialization::ProjectSerialization::saveProject(project, fresh.string()));
        checkStampedMembers(readText(fresh));

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("opening a newer engine's project keeps its record")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampOpenNewer");
        const auto projectFile = directory / "Game.vfproj";
        const std::string newer = "{\n"
                                  "  \"schemaVersion\": \"1.0\",\n"
                                  "  \"projectName\": \"StampTest\",\n"
                                  "  \"version\": \"1.0\",\n"
                                  "  \"workingDirectory\": \".\",\n"
                                  "  \"startupScene\": \"Startup.vfScene\",\n"
                                  "  \"engineVersion\": \"VertexForge 999.0.0\",\n"
                                  "  \"assetFormatVersion\": \"999.0.0\"\n"
                                  "}\n";
        writeBytes(projectFile, newer);

        services::ProjectServiceImpl service;
        service.setStampEngineVersionOnOpen(true);
        REQUIRE(service.loadProject(projectFile.string()));
        CHECK(readText(projectFile) == newer);
        REQUIRE(service.getCurrentProject().has_value());
        REQUIRE(service.getCurrentProject()->engineVersion.has_value());
        CHECK(*service.getCurrentProject()->engineVersion == "VertexForge 999.0.0");

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("a project whose engine stamp fails still opens")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_StampFails");
        const auto projectFile = directory / "Game.vfproj";
        const std::string original = foreignProject(true);
        writeBytes(projectFile, original);
        {
            ReadOnlyScope readOnly(projectFile);
            services::ProjectServiceImpl service;
            service.setStampEngineVersionOnOpen(true);
            CHECK(service.loadProject(projectFile.string()));
            CHECK(readText(projectFile) == original);
            CHECK_FALSE(stampTempExists(projectFile));
            REQUIRE(service.getCurrentProject().has_value());
            REQUIRE(service.getCurrentProject()->engineVersion.has_value());
            CHECK(*service.getCurrentProject()->engineVersion == olderEngineVersion);
        }

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }
}
