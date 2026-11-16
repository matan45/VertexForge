#include <doctest.h>

#include "config/ProjectConfig.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ProjectEvents.hpp"
#include "export/ExeIconEmbedder.hpp"
#include "impl/project/ProjectServiceImpl.hpp"
#include "launch/EditorArguments.hpp"
#include "launch/EditorStateDirectory.hpp"
#include "launch/LaunchStatusFile.hpp"
#include "launch/ProcessCodePage.hpp"
#include "launch/ProjectLock.hpp"
#include "launch/Utf8Path.hpp"
#include "serialization/ProjectSerialization.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "test_repo_scan_helpers.hpp" // pulls <windows.h>: keep it last

// ============================================================
// VK-1684: Editor.exe, Runtime.exe and Tests.exe embed utilities/launch/Utf8CodePage.manifest, so the
// process code page is UTF-8 (Windows 10 1903+). Every narrow string the engine hands to Windows,
// the CRT or std::filesystem is then UTF-8: a project may live in a folder with Hebrew or Cyrillic
// characters. These tests run in Tests.exe, which embeds the same manifest; the first one fails
// when it does not (premake5.lua lists the manifest in the Tests project's files {}).
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    // The same text as UTF-8 bytes and as UTF-16.
    struct Sample
    {
        const char* name;
        std::string utf8;
        std::wstring wide;
    };

    // "פרויקט" (project), "Игра" (game), and U+1F3AE (a game controller, outside the BMP: a
    // surrogate pair in UTF-16, 4 bytes in UTF-8).
    const Sample kHebrew{"Hebrew", "\xD7\xA4\xD7\xA8\xD7\x95\xD7\x99\xD7\xA7\xD7\x98",
                         L"פרויקט"};
    const Sample kCyrillic{"Cyrillic", "\xD0\x98\xD0\xB3\xD1\x80\xD0\xB0", L"Игра"};
    const Sample kEmoji{"emoji", "\xF0\x9F\x8E\xAE", L"\U0001F3AE"};

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

    // A temp folder whose name holds Hebrew and Cyrillic characters. Created through the wide path,
    // so the setup never depends on the code page under test.
    struct TempDir
    {
        fs::path path;

        explicit TempDir(std::wstring_view name)
        {
            path = fs::temp_directory_path() /
                   (L"VertexForge_Unicode_" + std::wstring(name) + L"_" + kHebrew.wide + L"_" + kCyrillic.wide);
            std::error_code ec;
            fs::remove_all(path, ec);
            fs::create_directories(path);
        }

        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };

    // RT_MANIFEST (24) #1, the manifest of an .exe; nullopt when there is none.
    std::optional<std::string> manifestOf(HMODULE module)
    {
        HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
        if (resource == nullptr)
        {
            return std::nullopt;
        }
        HGLOBAL loaded = LoadResource(module, resource);
        const DWORD size = SizeofResource(module, resource);
        const void* data = loaded != nullptr ? LockResource(loaded) : nullptr;
        if (data == nullptr || size == 0)
        {
            return std::nullopt;
        }
        return std::string(static_cast<const char*>(data), size);
    }

    std::optional<std::string> manifestOfFile(const fs::path& exe)
    {
        HMODULE module = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (module == nullptr)
        {
            return std::nullopt;
        }
        std::optional<std::string> manifest = manifestOf(module);
        FreeLibrary(module);
        return manifest;
    }

    // `<activeCodePage ...>UTF-8<`, however link.exe spells the element's namespace prefix.
    bool declaresUtf8CodePage(const std::string& manifest)
    {
        const std::size_t element = manifest.find("activeCodePage");
        if (element == std::string::npos)
        {
            return false;
        }
        const std::size_t close = manifest.find('>', element);
        if (close == std::string::npos)
        {
            return false;
        }
        std::size_t value = close + 1;
        while (value < manifest.size() &&
               (manifest[value] == ' ' || manifest[value] == '\t' || manifest[value] == '\r' || manifest[value] == '\n'))
        {
            ++value;
        }
        return manifest.compare(value, 5, "UTF-8") == 0;
    }

    // A one-image .ico: a 6-byte header, one 16-byte entry (1x1, 32 bpp, 8 bytes at offset 22) and
    // 8 bytes of image data. ExeIconEmbedder only copies the bytes, so they need not be a real image.
    constexpr unsigned char kTinyIcon[] = {0, 0, 1, 0, 1, 0,                                    // header
                                           1, 1, 0, 0, 1, 0, 32, 0, 8, 0, 0, 0, 22, 0, 0, 0,  // entry
                                           0, 0, 0, 0, 0, 0, 0, 0};                           // image
    static_assert(sizeof(kTinyIcon) == 30);
}

TEST_SUITE("UnicodePaths")
{
    TEST_CASE("Tests.exe runs in the UTF-8 code page")
    {
        INFO("Tests.exe must embed utilities/launch/Utf8CodePage.manifest (premake5.lua, files {} of the Tests "
             "project; re-run premake5 vs2022) and run on Windows 10 version 1903 or later");
        CHECK(GetACP() == CP_UTF8);
        CHECK(launch::activeCodePage() == CP_UTF8);
        CHECK(launch::processUsesUtf8());

        const std::optional<std::string> manifest = manifestOf(GetModuleHandleW(nullptr));
        REQUIRE(manifest.has_value());
        CHECK(declaresUtf8CodePage(*manifest));
        // Merged with the linker's default manifest, not instead of it.
        CHECK(manifest->find("asInvoker") != std::string::npos);
    }

    TEST_CASE("a narrow path string is UTF-8")
    {
        for (const Sample* sample : {&kHebrew, &kCyrillic, &kEmoji})
        {
            CAPTURE(sample->name);
            CHECK(fs::path(sample->utf8).native() == sample->wide);
            CHECK(fs::path(sample->wide).string() == sample->utf8);
            CHECK(launch::pathToUtf8(fs::path(sample->utf8)) == sample->utf8);
        }

        // Stricter than a single-byte ANSI code page: bytes that are not UTF-8 are refused, not guessed.
        CHECK_THROWS_AS(static_cast<void>(fs::path(std::string("caf\xE9"))), std::system_error);
        // An unpaired surrogate (a valid NTFS name) has no UTF-8 form.
        CHECK_THROWS_AS(static_cast<void>(fs::path(std::wstring(L"a\xD800" L"b")).string()), std::system_error);
    }

    // What stb_image, dr_libs, the mType file reader and the engine's own streams rely on.
    TEST_CASE("the CRT's narrow file functions take UTF-8 paths")
    {
        TempDir dir(L"Crt");
        const fs::path wideFile = dir.path / (kHebrew.wide + kEmoji.wide + L".txt");
        const std::string narrowFile = launch::pathToUtf8(wideFile);

        {
            std::ofstream out(narrowFile, std::ios::binary);
            REQUIRE(out.is_open());
            out << "stream";
        }
        CHECK(fs::exists(wideFile));
        CHECK(fs::exists(narrowFile));

        std::FILE* file = nullptr;
        REQUIRE(fopen_s(&file, narrowFile.c_str(), "ab") == 0);
        REQUIRE(file != nullptr);
        std::fputs("+crt", file);
        std::fclose(file);

        std::ifstream in(narrowFile, std::ios::binary);
        REQUIRE(in.is_open());
        std::string content;
        std::getline(in, content);
        CHECK(content == "stream+crt");
    }

    TEST_CASE("a project in a Hebrew and Cyrillic folder is saved, inspected, loaded and saved again")
    {
        DispatcherScope dispatcherScope;
        TempDir dir(L"Project");

        fs::create_directories(dir.path / L"scenes");
        const fs::path sceneFile = dir.path / L"scenes" / (kHebrew.wide + L".vfScene");
        {
            std::ofstream scene(sceneFile, std::ios::binary);
            scene << "{}";
        }

        const fs::path projectWide = dir.path / (kCyrillic.wide + L".vfproj");
        const std::string projectFile = launch::pathToUtf8(projectWide);

        config::ProjectConfig project;
        project.projectName = kHebrew.utf8;
        project.version = "1.0";
        project.workingDirectory = ".";
        project.startupScene = "scenes/" + kHebrew.utf8 + ".vfScene";
        REQUIRE(serialization::ProjectSerialization::saveProject(project, projectFile));

        // nlohmann writes the name as raw UTF-8, not as \u escapes.
        const std::string raw = repo_scan::readFile(projectWide);
        CHECK(raw.find("\"projectName\": \"" + kHebrew.utf8 + "\"") != std::string::npos);

        const serialization::ProjectInspection inspection = serialization::ProjectSerialization::inspectProject(projectFile);
        REQUIRE(inspection.ok());
        CHECK(inspection.config->projectName == kHebrew.utf8);
        CHECK(inspection.config->startupScene == project.startupScene);

        services::ProjectServiceImpl service;
        service.registerEventHandlers();
        REQUIRE(service.loadProject(projectFile));
        REQUIRE(service.getCurrentProject().has_value());
        const config::ProjectConfig loaded = *service.getCurrentProject();

        // "." resolves against the .vfproj's folder (ProjectServiceImpl::loadProject).
        std::error_code ec;
        CHECK(fs::equivalent(fs::path(loaded.workingDirectory), dir.path, ec));
        CHECK(fs::equivalent(fs::path(loaded.workingDirectory) / loaded.startupScene, sceneFile, ec));

        const std::optional<std::string> projectPath =
            events::EventDispatcher::instance().query(events::project::GetProjectPathQuery{});
        REQUIRE(projectPath.has_value());
        CHECK(*projectPath == projectFile);

        events::project::SaveProjectCommand save;
        save.filePath = "";
        CHECK(events::EventDispatcher::instance().execute(save));
        const serialization::ProjectInspection saved = serialization::ProjectSerialization::inspectProject(projectFile);
        REQUIRE(saved.ok());
        CHECK(saved.config->projectName == kHebrew.utf8);
    }

    TEST_CASE("the launch contract pieces work in a Hebrew and Cyrillic folder")
    {
        TempDir dir(L"Launch");
        const std::string project = launch::pathToUtf8(dir.path / (kHebrew.wide + L".vfproj"));
        const fs::path statusWide = dir.path / (kCyrillic.wide + L".status.json");
        const std::string status = launch::pathToUtf8(statusWide);

        const std::vector<std::string_view> arguments{project, "--launch-status", status};
        const launch::EditorArgumentsResult parsed = launch::parseEditorArguments(arguments);
        REQUIRE(parsed.ok());
        CHECK(parsed.args.projectPath == project);
        CHECK(parsed.args.launchStatusPath == status);

        // The narrow form, as editor/run/Main.cpp opens it.
        launch::LaunchStatusFile statusFile;
        REQUIRE(statusFile.open(fs::path(parsed.args.launchStatusPath)));
        REQUIRE(statusFile.write(launch::LaunchState::Loading, 0, "Loading project " + project));
        REQUIRE(fs::exists(statusWide));
        const json line = json::parse(repo_scan::readFile(statusWide));
        CHECK(line.at("state").get<std::string>() == "loading");
        CHECK(line.at("message").get<std::string>() == "Loading project " + project);

        launch::ProjectLockAttempt attempt = launch::tryAcquireProjectLock(dir.path);
        CHECK(attempt.status == launch::ProjectLockAttempt::Status::Acquired);
        CHECK(fs::exists(dir.path / launch::editorStateDirName / launch::editorLockFileName));
        attempt.lock.release();
    }

    TEST_CASE("Editor.exe and Runtime.exe embed the UTF-8 code page manifest")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "repo root not found");
        for (const char* app : {"Editor", "Runtime"})
        {
            CAPTURE(app);
            const fs::path exe = *root / "bin" / app / repo_scan::thisConfiguration() / "x64" / (std::string(app) + ".exe");
            if (!fs::exists(exe))
            {
                MESSAGE("skipped: " << exe.string() << " not found (build " << app << " in "
                                    << repo_scan::thisConfiguration() << ")");
                continue;
            }
            const std::optional<std::string> manifest = manifestOfFile(exe);
            REQUIRE_MESSAGE(manifest.has_value(), "no RT_MANIFEST #1 in " << exe.string());
            CHECK(declaresUtf8CodePage(*manifest));
            CHECK(manifest->find("asInvoker") != std::string::npos);
        }
    }

    // GameExporter copies Runtime.exe under the game's name, then ExeIconEmbedder swaps the icon:
    // the shipped game must still run in the UTF-8 code page.
    TEST_CASE("an exported game keeps the manifest through the copy and the icon swap")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "repo root not found");
        const fs::path runtime = *root / "bin" / "Runtime" / repo_scan::thisConfiguration() / "x64" / "Runtime.exe";
        if (!fs::exists(runtime))
        {
            MESSAGE("skipped: " << runtime.string() << " not found (build Runtime in "
                                << repo_scan::thisConfiguration() << ")");
            return;
        }

        TempDir dir(L"Export");
        const fs::path game = dir.path / (kHebrew.wide + L".exe");
        fs::copy_file(runtime, game);
        const fs::path icon = dir.path / L"icon.ico";
        {
            std::ofstream out(icon, std::ios::binary);
            out.write(reinterpret_cast<const char*>(kTinyIcon), sizeof(kTinyIcon));
        }

        std::string error;
        REQUIRE_MESSAGE(gameExport::ExeIconEmbedder::embedIcon(game, icon, error), error);
        const std::optional<std::string> manifest = manifestOfFile(game);
        REQUIRE(manifest.has_value());
        CHECK(declaresUtf8CodePage(*manifest));
    }
}
