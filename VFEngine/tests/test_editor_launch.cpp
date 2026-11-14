#include <doctest.h>

#include "config/ProjectConfig.hpp"
#include "launch/EditorArguments.hpp"
#include "launch/EditorExitCode.hpp"
#include "launch/EditorStateDirectory.hpp"
#include "launch/LaunchStatusFile.hpp"
#include "launch/ProjectLock.hpp"
#include "serialization/ProjectSerialization.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "test_repo_scan_helpers.hpp" // pulls <windows.h>: keep it last

// ============================================================
// VK-1682: the Editor.exe launch contract. Exit codes, strict argument parsing, the
// pre-init .vfproj inspection (exit 3 vs 4), the --launch-status file format and its
// atomic writer, and the .vertexforge/editor.lock that keeps a second editor out.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;
    using launch::LaunchState;
    using serialization::ProjectInspectionStatus;
    using serialization::ProjectSerialization;

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

    launch::EditorArgumentsResult parse(std::initializer_list<std::string_view> arguments)
    {
        const std::vector<std::string_view> list(arguments);
        return launch::parseEditorArguments(list);
    }

    bool hasErrorContaining(const launch::EditorArgumentsResult& result, std::string_view text)
    {
        return std::any_of(result.errors.begin(), result.errors.end(),
                           [text](const std::string& error) { return error.find(text) != std::string::npos; });
    }

    void writeText(const fs::path& path, const std::string& text)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }

    json validProjectJson()
    {
        return json{
            {"schemaVersion", config::ProjectSchemaVersion::toString()},
            {"projectName", "Demo"},
            {"version", "1.0"},
            {"workingDirectory", "."},
            {"startupScene", "scenes/Main.vfScene"}
        };
    }

    fs::path writeProject(const fs::path& folder, const json& project)
    {
        const fs::path file = folder / "Demo.vfproj";
        writeText(file, project.dump(2));
        return file;
    }

    std::string formatted(LaunchState state, int code, std::string_view message, unsigned long pid = 42)
    {
        char buffer[launch::launchStatusBufferSize];
        const std::size_t length = launch::formatLaunchStatus(buffer, sizeof(buffer), state, code, message, pid);
        return std::string(buffer, length);
    }

    std::string formattedMessage(std::string_view message)
    {
        const json parsed = json::parse(formatted(LaunchState::Loading, 0, message));
        return parsed.at("message").get<std::string>();
    }

    json readStatus(const fs::path& file)
    {
        return json::parse(repo_scan::readFile(file));
    }

    std::vector<std::string> fileNamesIn(const fs::path& folder)
    {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(folder, ec))
        {
            names.push_back(entry.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    fs::path lockFileOf(const fs::path& projectFolder)
    {
        return projectFolder / launch::editorStateDirName / launch::editorLockFileName;
    }

    fs::path upperCased(const fs::path& path)
    {
        std::wstring text = path.native();
        std::transform(text.begin(), text.end(), text.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
        return fs::path(text);
    }
}

TEST_SUITE("EditorLaunch")
{
    // --------------------------------------------------------------- exit codes

    TEST_CASE("exit codes keep their contract values")
    {
        CHECK(launch::toInt(launch::EditorExitCode::Ok) == 0);
        CHECK(launch::toInt(launch::EditorExitCode::BadArguments) == 2);
        CHECK(launch::toInt(launch::EditorExitCode::ProjectLoadFailed) == 3);
        CHECK(launch::toInt(launch::EditorExitCode::ProjectSchemaTooNew) == 4);
        CHECK(launch::toInt(launch::EditorExitCode::ProjectAlreadyOpen) == 5);
    }

    TEST_CASE("the launcher's EditorExitCode.java carries the same values")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "repo root not found");
        const fs::path javaFile =
            *root / "launcher" / "src" / "main" / "java" / "com" / "vertexforge" / "launcher" / "service" / "EditorExitCode.java";
        REQUIRE(fs::exists(javaFile));
        const std::string source = repo_scan::stripComments(repo_scan::readFile(javaFile));

        const std::pair<const char*, launch::EditorExitCode> pairs[] = {
            {"OK", launch::EditorExitCode::Ok},
            {"BAD_ARGUMENTS", launch::EditorExitCode::BadArguments},
            {"PROJECT_LOAD_FAILED", launch::EditorExitCode::ProjectLoadFailed},
            {"PROJECT_SCHEMA_TOO_NEW", launch::EditorExitCode::ProjectSchemaTooNew},
            {"PROJECT_ALREADY_OPEN", launch::EditorExitCode::ProjectAlreadyOpen},
        };
        for (const auto& [name, code] : pairs)
        {
            const std::string constant = std::string(name) + "(" + std::to_string(launch::toInt(code)) + ",";
            CHECK_MESSAGE(source.find(constant) != std::string::npos, "missing in EditorExitCode.java: " << constant);
        }
    }

    // ---------------------------------------------------------------- arguments

    TEST_CASE("no arguments start without a project")
    {
        const auto result = parse({});
        CHECK(result.ok());
        CHECK(result.args.projectPath.empty());
        CHECK(result.args.launchStatusPath.empty());
        CHECK_FALSE(result.args.mcpEnable);
        CHECK_FALSE(result.args.mcpPort.has_value());
        CHECK_FALSE(result.args.mcpToken.has_value());
    }

    TEST_CASE("every flag in any order")
    {
        const auto result = parse({"--mcp-port", "7878", "C:/Games/Demo/Demo.vfproj", "--launch-status", "status.json",
                                   "--mcp", "--mcp-token", "secret"});
        REQUIRE(result.ok());
        CHECK(result.args.projectPath == "C:/Games/Demo/Demo.vfproj");
        CHECK(result.args.launchStatusPath == "status.json");
        CHECK(result.args.mcpEnable);
        REQUIRE(result.args.mcpPort.has_value());
        CHECK(*result.args.mcpPort == 7878);
        REQUIRE(result.args.mcpToken.has_value());
        CHECK(*result.args.mcpToken == "secret");
    }

    TEST_CASE("an empty token is accepted as before")
    {
        const auto result = parse({"--mcp-token", ""});
        REQUIRE(result.ok());
        REQUIRE(result.args.mcpToken.has_value());
        CHECK(result.args.mcpToken->empty());
    }

    TEST_CASE("unknown flags are errors")
    {
        CHECK(hasErrorContaining(parse({"--bogus"}), "unknown argument --bogus"));
        CHECK(hasErrorContaining(parse({"-x"}), "unknown argument -x"));
        CHECK(hasErrorContaining(parse({"--"}), "unknown argument --"));
    }

    TEST_CASE("a missing value is an error and the next flag is not swallowed")
    {
        CHECK(hasErrorContaining(parse({"--mcp-port"}), "--mcp-port needs a value"));
        CHECK(hasErrorContaining(parse({"--launch-status"}), "--launch-status needs a value"));

        const auto result = parse({"--mcp-token", "--mcp"});
        CHECK(hasErrorContaining(result, "--mcp-token needs a value"));
        CHECK_FALSE(result.args.mcpToken.has_value());
        CHECK(result.args.mcpEnable);  // --mcp was still read as a flag
    }

    TEST_CASE("an invalid port is an error")
    {
        for (const char* value : {"80", "1023", "65536", "abc", "7878x", "-1"})
        {
            const auto result = parse({"--mcp-port", value});
            CHECK_MESSAGE(hasErrorContaining(result, "not a port in 1024-65535"), value);
            CHECK_FALSE(result.args.mcpPort.has_value());
        }
    }

    TEST_CASE("a repeated flag is an error and the first value is kept")
    {
        CHECK(hasErrorContaining(parse({"--mcp", "--mcp"}), "--mcp is given more than once"));

        const auto status = parse({"--launch-status", "a.json", "--launch-status", "b.json"});
        CHECK(hasErrorContaining(status, "--launch-status is given more than once"));
        CHECK(status.args.launchStatusPath == "a.json");

        const auto port = parse({"--mcp-port", "2000", "--mcp-port", "3000"});
        CHECK(hasErrorContaining(port, "--mcp-port is given more than once"));
        REQUIRE(port.args.mcpPort.has_value());
        CHECK(*port.args.mcpPort == 2000);
    }

    TEST_CASE("a second or empty positional argument is an error")
    {
        const auto twice = parse({"A.vfproj", "B.vfproj"});
        CHECK(hasErrorContaining(twice, "unexpected extra argument B.vfproj"));
        CHECK(twice.args.projectPath == "A.vfproj");

        CHECK(hasErrorContaining(parse({""}), "empty argument"));
        CHECK(hasErrorContaining(parse({"--launch-status", ""}), "--launch-status needs a file path"));
    }

    TEST_CASE("the status path survives other argument errors")
    {
        const auto result = parse({"--bogus", "--launch-status", "status.json", "A.vfproj", "B.vfproj"});
        CHECK_FALSE(result.ok());
        CHECK(result.errors.size() == 2);
        CHECK(result.args.launchStatusPath == "status.json");
    }

    TEST_CASE("parseMcpPort keeps the editor's old rules")
    {
        CHECK(launch::parseMcpPort("1024").value_or(0) == 1024);
        CHECK(launch::parseMcpPort("7878").value_or(0) == 7878);
        CHECK(launch::parseMcpPort("65535").value_or(0) == 65535);
        for (const char* bad : {"", "1023", "65536", "+7878", " 7878", "7878 ", "0x1F90", "99999999999"})
        {
            CHECK_MESSAGE(!launch::parseMcpPort(bad).has_value(), bad);
        }
        CHECK_FALSE(launch::editorUsage().empty());
    }

    // ------------------------------------------------------- project inspection

    TEST_CASE("a valid project inspects Ok")
    {
        TempDir temp("VertexForge_EditorLaunch_InspectOk");
        const auto inspection = ProjectSerialization::inspectProject(writeProject(temp.path, validProjectJson()).string());
        REQUIRE(inspection.ok());
        REQUIRE(inspection.config.has_value());
        CHECK(inspection.config->projectName == "Demo");
        CHECK(inspection.message.empty());
        CHECK(inspection.warnings.empty());
    }

    TEST_CASE("a missing project file is unreadable, and the message carries no path")
    {
        TempDir temp("VertexForge_EditorLaunch_InspectMissing");
        const fs::path missing = temp.path / "Nope.vfproj";
        const auto inspection = ProjectSerialization::inspectProject(missing.string());
        CHECK(inspection.status == ProjectInspectionStatus::FileUnreadable);
        CHECK_FALSE(inspection.config.has_value());
        CHECK_FALSE(inspection.message.empty());
        CHECK(inspection.message.find("Nope.vfproj") == std::string::npos);
        CHECK_FALSE(ProjectSerialization::loadProject(missing.string()).has_value());
    }

    TEST_CASE("invalid JSON is a parse error")
    {
        TempDir temp("VertexForge_EditorLaunch_InspectParse");
        const fs::path file = temp.path / "Demo.vfproj";
        writeText(file, "{ \"schemaVersion\": ");
        CHECK(ProjectSerialization::inspectProject(file.string()).status == ProjectInspectionStatus::ParseError);
    }

    TEST_CASE("structure errors name the field")
    {
        TempDir temp("VertexForge_EditorLaunch_InspectStructure");

        const fs::path arrayFile = temp.path / "Array.vfproj";
        writeText(arrayFile, "[]");
        const auto rootInspection = ProjectSerialization::inspectProject(arrayFile.string());
        CHECK(rootInspection.status == ProjectInspectionStatus::InvalidStructure);
        CHECK(rootInspection.message.find("root is not a JSON object") != std::string::npos);

        for (const char* field : {"schemaVersion", "projectName", "version", "workingDirectory", "startupScene"})
        {
            json project = validProjectJson();
            project.erase(field);
            const auto inspection = ProjectSerialization::inspectProject(writeProject(temp.path, project).string());
            CHECK_MESSAGE(inspection.status == ProjectInspectionStatus::InvalidStructure, field);
            CHECK_MESSAGE(inspection.message.find(field) != std::string::npos, field);
        }

        json empty = validProjectJson();
        empty["projectName"] = "";
        const auto emptyInspection = ProjectSerialization::inspectProject(writeProject(temp.path, empty).string());
        CHECK(emptyInspection.status == ProjectInspectionStatus::InvalidStructure);
        CHECK(emptyInspection.message.find("'projectName' cannot be empty") != std::string::npos);
    }

    TEST_CASE("schema versions must be MAJOR.MINOR in decimal digits")
    {
        TempDir temp("VertexForge_EditorLaunch_InspectVersionFormat");
        for (const char* version : {"1", "-1.0", "1.0.0", " 1.0", "1.", ".0", "1.x", "a.b", "+1.0"})
        {
            json project = validProjectJson();
            project["schemaVersion"] = version;
            const auto inspection = ProjectSerialization::inspectProject(writeProject(temp.path, project).string());
            CHECK_MESSAGE(inspection.status == ProjectInspectionStatus::InvalidSchemaVersion, version);
        }
    }

    TEST_CASE("an older or newer schema major is refused with its own status")
    {
        TempDir temp("VertexForge_EditorLaunch_InspectSchema");
        const uint32_t major = config::ProjectSchemaVersion::major;

        json newer = validProjectJson();
        const std::string newerText = std::to_string(major + 1) + ".0";
        newer["schemaVersion"] = newerText;
        const auto newerInspection = ProjectSerialization::inspectProject(writeProject(temp.path, newer).string());
        CHECK(newerInspection.status == ProjectInspectionStatus::SchemaTooNew);
        CHECK(newerInspection.message.find(newerText) != std::string::npos);
        CHECK(newerInspection.message.find("newer") != std::string::npos);

        REQUIRE(major > 0);
        json older = validProjectJson();
        older["schemaVersion"] = std::to_string(major - 1) + ".9";
        CHECK(ProjectSerialization::inspectProject(writeProject(temp.path, older).string()).status ==
              ProjectInspectionStatus::SchemaTooOld);

        // A major too large for 32 bits is still newer, not malformed.
        json huge = validProjectJson();
        huge["schemaVersion"] = "4294967296.0";
        CHECK(ProjectSerialization::inspectProject(writeProject(temp.path, huge).string()).status ==
              ProjectInspectionStatus::SchemaTooNew);

        // A newer MINOR stays compatible (tests/test_project_config.cpp pins the same rule).
        json newerMinor = validProjectJson();
        newerMinor["schemaVersion"] = std::to_string(major) + ".99";
        CHECK(ProjectSerialization::inspectProject(writeProject(temp.path, newerMinor).string()).ok());
    }

    TEST_CASE("ignored values are warnings, not failures")
    {
        TempDir temp("VertexForge_EditorLaunch_InspectWarnings");
        json project = validProjectJson();
        project["fontFallbackChain"] = "not-an-array";
        const auto inspection = ProjectSerialization::inspectProject(writeProject(temp.path, project).string());
        CHECK(inspection.ok());
        REQUIRE(inspection.warnings.size() == 1);
        CHECK(inspection.warnings[0].find("fontFallbackChain") != std::string::npos);
    }

    // ------------------------------------------------------ status line format

    TEST_CASE("the status line has the documented shape")
    {
        CHECK(formatted(LaunchState::Loading, 0, "Loading project", 42) ==
              "{\"state\":\"loading\",\"code\":0,\"message\":\"Loading project\",\"pid\":42}\n");
        CHECK(formatted(LaunchState::Ready, 0, "", 7) == "{\"state\":\"ready\",\"code\":0,\"message\":\"\",\"pid\":7}\n");
        CHECK(formatted(LaunchState::Failed, -1073741819, "crash", 4294967295ul) ==
              "{\"state\":\"failed\",\"code\":-1073741819,\"message\":\"crash\",\"pid\":4294967295}\n");
    }

    TEST_CASE("quotes, backslashes and control characters are escaped")
    {
        CHECK(formatted(LaunchState::Ready, 0, "a\"b\\c\n\t\x01", 1) ==
              "{\"state\":\"ready\",\"code\":0,\"message\":\"a\\\"b\\\\c\\u000a\\u0009\\u0001\",\"pid\":1}\n");
        CHECK(formattedMessage("C:\\Games\\\"Demo\"\r\n") == "C:\\Games\\\"Demo\"\r\n");
    }

    TEST_CASE("valid UTF-8 is kept and invalid bytes become '?'")
    {
        const std::string hebrew = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";            // shalom
        const std::string emoji = "\xF0\x9F\x8E\xAE";                              // U+1F3AE
        CHECK(formattedMessage("Project " + hebrew + " " + emoji) == "Project " + hebrew + " " + emoji);

        CHECK(formattedMessage("a\xFF" "b") == "a?b");
        CHECK(formattedMessage("cut\xC3") == "cut?");                             // truncated sequence
        CHECK(formattedMessage("\xC0\xAF") == "??");                              // overlong '/'
        CHECK(formattedMessage("\xED\xA0\x80") == "???");                         // UTF-16 surrogate
        CHECK(formattedMessage("\xF4\x90\x80\x80") == "????");                    // beyond U+10FFFF
        CHECK(formattedMessage("\x80") == "?");                                   // stray continuation
    }

    TEST_CASE("long messages are cut on a UTF-8 boundary")
    {
        CHECK(formattedMessage(std::string(2000, 'x')) == std::string(launch::maxLaunchStatusMessageBytes, 'x'));

        const std::string almost(launch::maxLaunchStatusMessageBytes - 1, 'x');
        CHECK(formattedMessage(almost + "\xC3\xA9") == almost);  // the 2-byte letter would cross the limit
    }

    TEST_CASE("every byte escaped still fits the buffer")
    {
        const std::string controls(launch::maxLaunchStatusMessageBytes, '\x01');
        const std::string line = formatted(LaunchState::Failed, -2147483647 - 1, controls, 4294967295ul);
        REQUIRE_FALSE(line.empty());
        CHECK(json::parse(line).at("message").get<std::string>() == controls);
    }

    TEST_CASE("a buffer that is too small yields nothing")
    {
        char tooSmall[10];  // not `small`: <windows.h> defines it as a macro
        CHECK(launch::formatLaunchStatus(tooSmall, sizeof(tooSmall), LaunchState::Ready, 0, "hello", 1) == 0);
        CHECK(tooSmall[0] == '\0');
        CHECK(launch::formatLaunchStatus(nullptr, 0, LaunchState::Ready, 0, "hello", 1) == 0);
    }

    // ------------------------------------------------------- status file writer

    TEST_CASE("open refuses a path it cannot write")
    {
        TempDir temp("VertexForge_EditorLaunch_StatusOpen");

        launch::LaunchStatusFile missingFolder;
        CHECK_FALSE(missingFolder.open(temp.path / "no-such-folder" / "status.json"));
        CHECK_FALSE(missingFolder.isOpen());

        launch::LaunchStatusFile folder;
        CHECK_FALSE(folder.open(temp.path));

        launch::LaunchStatusFile trailingSeparator;
        CHECK_FALSE(trailingSeparator.open(fs::path(temp.path.native() + L"\\")));

        launch::LaunchStatusFile notOpen;
        CHECK_FALSE(notOpen.write(LaunchState::Ready, 0, "x"));
        CHECK_FALSE(notOpen.writeFromCrash(1, "x"));

        launch::LaunchStatusFile twice;
        CHECK(twice.open(temp.path / "status.json"));
        CHECK_FALSE(twice.open(temp.path / "other.json"));
        CHECK(twice.path() == (temp.path / "status.json").lexically_normal());
    }

    TEST_CASE("writes replace the whole file and leave no temp file behind")
    {
        TempDir temp("VertexForge_EditorLaunch_StatusWrite");
        const fs::path statusPath = temp.path / "status.json";

        launch::LaunchStatusFile status;
        REQUIRE(status.open(statusPath));

        REQUIRE(status.write(LaunchState::Loading, 0, "Loading project Demo"));
        json line = readStatus(statusPath);
        CHECK(line.at("state").get<std::string>() == "loading");
        CHECK(line.at("code").get<int>() == 0);
        CHECK(line.at("message").get<std::string>() == "Loading project Demo");
        CHECK(line.at("pid").get<unsigned long>() == static_cast<unsigned long>(GetCurrentProcessId()));

        REQUIRE(status.write(LaunchState::Ready, 0, "Project Demo is ready"));
        line = readStatus(statusPath);
        CHECK(line.at("state").get<std::string>() == "ready");
        CHECK(line.at("message").get<std::string>() == "Project Demo is ready");

        CHECK(fileNamesIn(temp.path) == std::vector<std::string>{"status.json"});
    }

    TEST_CASE("the first failure is final")
    {
        TempDir temp("VertexForge_EditorLaunch_StatusLatch");
        const fs::path statusPath = temp.path / "status.json";

        launch::LaunchStatusFile status;
        REQUIRE(status.open(statusPath));
        REQUIRE(status.write(LaunchState::Loading, 0, "Loading"));
        REQUIRE(status.write(LaunchState::Failed, 3, "cannot open project"));

        CHECK_FALSE(status.write(LaunchState::Ready, 0, "ready"));
        CHECK_FALSE(status.write(LaunchState::Failed, 5, "another failure"));
        CHECK_FALSE(status.writeFromCrash(-1073741819, "crash"));

        const json line = readStatus(statusPath);
        CHECK(line.at("state").get<std::string>() == "failed");
        CHECK(line.at("code").get<int>() == 3);
        CHECK(line.at("message").get<std::string>() == "cannot open project");
    }

    TEST_CASE("a crash write lands once and blocks later writes")
    {
        TempDir temp("VertexForge_EditorLaunch_StatusCrash");
        const fs::path statusPath = temp.path / "status.json";

        launch::LaunchStatusFile status;
        REQUIRE(status.open(statusPath));
        REQUIRE(status.write(LaunchState::Ready, 0, "ready"));

        CHECK(status.writeFromCrash(-1073741819, "Editor crashed: ACCESS_VIOLATION"));
        CHECK_FALSE(status.writeFromCrash(-1073741819, "a second crash"));
        CHECK_FALSE(status.write(LaunchState::Ready, 0, "ready again"));
        CHECK_FALSE(status.writeFromCrash(1, nullptr));

        const json line = readStatus(statusPath);
        CHECK(line.at("state").get<std::string>() == "failed");
        CHECK(line.at("code").get<int>() == -1073741819);
        CHECK(line.at("message").get<std::string>() == "Editor crashed: ACCESS_VIOLATION");
        CHECK(fileNamesIn(temp.path) == std::vector<std::string>{"status.json"});
    }

    TEST_CASE("a reader that keeps the file open makes the write fail cleanly")
    {
        TempDir temp("VertexForge_EditorLaunch_StatusReader");
        const fs::path statusPath = temp.path / "status.json";

        launch::LaunchStatusFile status;
        REQUIRE(status.open(statusPath));
        REQUIRE(status.write(LaunchState::Loading, 0, "Loading"));

        {
            // Any open reader blocks the replace (MoveFileExW: ERROR_ACCESS_DENIED).
            std::ifstream reader(statusPath, std::ios::binary);
            REQUIRE(reader.is_open());
            CHECK_FALSE(status.write(LaunchState::Ready, 0, "ready"));
        }

        CHECK(readStatus(statusPath).at("state").get<std::string>() == "loading");
        CHECK(fileNamesIn(temp.path) == std::vector<std::string>{"status.json"});

        CHECK(status.write(LaunchState::Ready, 0, "ready"));
        CHECK(readStatus(statusPath).at("state").get<std::string>() == "ready");
    }

    TEST_CASE("a failure that could not be written does not close the file")
    {
        TempDir temp("VertexForge_EditorLaunch_StatusLostFailure");
        const fs::path statusPath = temp.path / "status.json";

        launch::LaunchStatusFile status;
        REQUIRE(status.open(statusPath));
        REQUIRE(status.write(LaunchState::Ready, 0, "ready"));

        {
            std::ifstream reader(statusPath, std::ios::binary);
            REQUIRE(reader.is_open());
            CHECK_FALSE(status.write(LaunchState::Failed, 3, "lost"));
        }

        CHECK(status.writeFromCrash(-1073741819, "Editor crashed: ACCESS_VIOLATION"));
        const json line = readStatus(statusPath);
        CHECK(line.at("state").get<std::string>() == "failed");
        CHECK(line.at("code").get<int>() == -1073741819);
    }

    // -------------------------------------------------------------- project lock

    TEST_CASE("the lock is exclusive and its pid stays readable")
    {
        TempDir temp("VertexForge_EditorLaunch_Lock");
        const unsigned long ourPid = GetCurrentProcessId();

        launch::ProjectLockAttempt first = launch::tryAcquireProjectLock(temp.path);
        REQUIRE(first.status == launch::ProjectLockAttempt::Status::Acquired);
        CHECK(first.lock.isHeld());
        CHECK(first.lock.hasFileIdentity());  // NTFS always has one
        CHECK(first.message.empty());
        CHECK(fs::is_regular_file(lockFileOf(temp.path)));
        CHECK(first.lock.holdsFile(lockFileOf(temp.path)));
        CHECK(first.lock.holdsFile(upperCased(lockFileOf(temp.path))));
        CHECK_FALSE(first.lock.holdsFile(temp.path / "nothing.lock"));

        // Readable while held: the lock covers a byte far beyond the content.
        const std::string content = repo_scan::readFile(lockFileOf(temp.path));
        CHECK(content == "{\"pid\":" + std::to_string(ourPid) + "}\n");

        // Locks belong to the handle: a second acquire in this process stands for a second editor.
        const launch::ProjectLockAttempt second = launch::tryAcquireProjectLock(temp.path);
        CHECK(second.status == launch::ProjectLockAttempt::Status::HeldByOther);
        CHECK_FALSE(second.lock.isHeld());
        REQUIRE(second.ownerPid.has_value());
        CHECK(*second.ownerPid == ourPid);
        CHECK(second.message.find("already open in another editor") != std::string::npos);
        CHECK(second.message.find("pid " + std::to_string(ourPid)) != std::string::npos);

        first.lock.release();
        CHECK_FALSE(first.lock.isHeld());
        CHECK(fs::exists(lockFileOf(temp.path)));  // never deleted

        launch::ProjectLockAttempt third = launch::tryAcquireProjectLock(temp.path);
        CHECK(third.status == launch::ProjectLockAttempt::Status::Acquired);
    }

    TEST_CASE("the lock moves with its owner")
    {
        TempDir temp("VertexForge_EditorLaunch_LockMove");
        launch::ProjectLockAttempt acquired = launch::tryAcquireProjectLock(temp.path);
        REQUIRE(acquired.status == launch::ProjectLockAttempt::Status::Acquired);

        launch::ProjectLock moved = std::move(acquired.lock);
        CHECK_FALSE(acquired.lock.isHeld());
        CHECK(moved.isHeld());
        CHECK(launch::tryAcquireProjectLock(temp.path).status == launch::ProjectLockAttempt::Status::HeldByOther);

        launch::ProjectLock assigned;
        assigned = std::move(moved);
        CHECK_FALSE(moved.isHeld());
        CHECK(assigned.isHeld());
        assigned.release();
        CHECK(launch::tryAcquireProjectLock(temp.path).status == launch::ProjectLockAttempt::Status::Acquired);
    }

    TEST_CASE("a project that cannot be locked is Unavailable, never refused")
    {
        TempDir temp("VertexForge_EditorLaunch_LockUnavailable");

        SUBCASE("read-only lock file")
        {
            const fs::path stateFolder = temp.path / launch::editorStateDirName;
            fs::create_directories(stateFolder);
            writeText(lockFileOf(temp.path), "");
            // MSVC sets FILE_ATTRIBUTE_READONLY only when ALL write bits are removed.
            fs::permissions(lockFileOf(temp.path), fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                            fs::perm_options::remove);
            REQUIRE((fs::status(lockFileOf(temp.path)).permissions() & fs::perms::owner_write) == fs::perms::none);

            const auto attempt = launch::tryAcquireProjectLock(temp.path);
            CHECK(attempt.status == launch::ProjectLockAttempt::Status::Unavailable);
            CHECK_FALSE(attempt.lock.isHeld());
            CHECK_FALSE(attempt.message.empty());

            fs::permissions(lockFileOf(temp.path), fs::perms::owner_write, fs::perm_options::add);
        }

        SUBCASE(".vertexforge is a file")
        {
            writeText(temp.path / launch::editorStateDirName, "not a folder");
            const auto attempt = launch::tryAcquireProjectLock(temp.path);
            CHECK(attempt.status == launch::ProjectLockAttempt::Status::Unavailable);
        }

        SUBCASE("missing project folder: nothing is created")
        {
            const fs::path missing = temp.path / "missing";
            const auto attempt = launch::tryAcquireProjectLock(missing);
            CHECK(attempt.status == launch::ProjectLockAttempt::Status::Unavailable);
            CHECK_FALSE(fs::exists(missing));
        }
    }

    TEST_CASE("the keeper keeps, moves and refuses")
    {
        TempDir temp("VertexForge_EditorLaunch_Keeper");
        const fs::path folderA = temp.path / "A";
        const fs::path folderB = temp.path / "B";
        fs::create_directories(folderA);
        fs::create_directories(folderB);
        const fs::path projectA = folderA / "Demo.vfproj";
        const fs::path projectB = folderB / "Demo.vfproj";

        using Outcome = launch::ProjectLockKeeper::Result::Outcome;

        SUBCASE("the same folder, however spelled, is kept")
        {
            launch::ProjectLockKeeper keeper;
            CHECK(keeper.switchTo(projectA).outcome == Outcome::Acquired);
            CHECK(keeper.switchTo(projectA).outcome == Outcome::Kept);
            CHECK(keeper.switchTo(folderA / "." / "Other.vfproj").outcome == Outcome::Kept);
            CHECK(keeper.switchTo(upperCased(projectA)).outcome == Outcome::Kept);
            CHECK(keeper.current().isHeld());
        }

        SUBCASE("switching releases the previous project")
        {
            launch::ProjectLockKeeper keeper;
            REQUIRE(keeper.switchTo(projectA).outcome == Outcome::Acquired);
            REQUIRE(keeper.switchTo(projectB).outcome == Outcome::Acquired);

            launch::ProjectLockKeeper other;
            CHECK(other.switchTo(projectA).outcome == Outcome::Acquired);
            CHECK(other.switchTo(projectB).outcome == Outcome::HeldByOther);
        }

        SUBCASE("a folder held by another editor is refused and the current lock stays")
        {
            launch::ProjectLockKeeper keeper;
            REQUIRE(keeper.switchTo(projectA).outcome == Outcome::Acquired);

            launch::ProjectLockKeeper other;
            REQUIRE(other.switchTo(projectB).outcome == Outcome::Acquired);

            const auto refused = keeper.switchTo(projectB);
            CHECK(refused.outcome == Outcome::HeldByOther);
            CHECK(refused.ownerPid.has_value());
            CHECK_FALSE(refused.message.empty());
            CHECK(keeper.current().isHeld());

            launch::ProjectLockKeeper third;
            CHECK(third.switchTo(projectA).outcome == Outcome::HeldByOther);
        }

        SUBCASE("an unlockable folder releases the previous project")
        {
            writeText(folderB / launch::editorStateDirName, "not a folder");

            launch::ProjectLockKeeper keeper;
            REQUIRE(keeper.switchTo(projectA).outcome == Outcome::Acquired);
            CHECK(keeper.switchTo(projectB).outcome == Outcome::Unavailable);
            CHECK_FALSE(keeper.current().isHeld());

            // Remembered: the startup check and the open guard must not warn twice.
            CHECK(keeper.switchTo(projectB).outcome == Outcome::Kept);

            launch::ProjectLockKeeper other;
            CHECK(other.switchTo(projectA).outcome == Outcome::Acquired);
        }

        SUBCASE("release frees the folder")
        {
            launch::ProjectLockKeeper keeper;
            REQUIRE(keeper.switchTo(projectA).outcome == Outcome::Acquired);
            keeper.release();
            CHECK_FALSE(keeper.current().isHeld());

            launch::ProjectLockKeeper other;
            CHECK(other.switchTo(projectA).outcome == Outcome::Acquired);
        }
    }

    TEST_CASE("the editor state folder is recognised by name")
    {
        CHECK(launch::isEditorStateDirectory(fs::path(L"C:\\Games\\Demo\\.vertexforge")));
        CHECK_FALSE(launch::isEditorStateDirectory(fs::path(L"C:\\Games\\Demo\\assets")));
        CHECK_FALSE(launch::isEditorStateDirectory(fs::path(L"C:\\Games\\Demo\\.vertexforge\\editor.lock")));
    }
}
