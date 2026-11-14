#include "../handlers/EditorHandler.hpp"
#include "../splash/SplashScreen.hpp"
#include "crash/CrashHandler.hpp"
#include "launch/EditorArguments.hpp"
#include "launch/EditorExitCode.hpp"
#include "launch/LaunchStatusFile.hpp"
#include "launch/ProjectLock.hpp"
#include "launch/Utf8Path.hpp"
#include "print/Log.hpp"
#include "serialization/ProjectSerialization.hpp"
#include <csignal>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Editor.exe [project.vfproj] [--launch-status <file>] [--mcp] [--mcp-port <n>] [--mcp-token <t>]
//
// The first non-flag argument is the project path; flags may appear in any order. Without
// arguments the editor starts with no project.
//
// Launch contract (VK-1682), relied on by the launcher:
//
// Exit codes (launch::EditorExitCode; the launcher's twin is service/EditorExitCode.java):
//   0  normal exit
//   2  bad arguments: an unknown flag, a missing value, an invalid --mcp-port, a repeated flag, a
//      second positional argument, or a --launch-status file that cannot be written
//   3  the .vfproj is missing, unreadable or invalid (checked before the main window opens; a load
//      that still fails after the editor started exits 3 too)
//   4  the .vfproj schema major is newer than this editor's
//   5  another editor has the project open (its .vertexforge/editor.lock is held)
//   A crash exits with its exception code (0xC0000005, ...). In a Debug build the CRT assertion
//   dialog's Abort ends the process with 3 as well; the status file tells the two apart.
//
// --launch-status <file>: one line of JSON, replaced atomically at every step:
//   {"state":"loading"|"ready"|"failed","code":N,"message":"...","pid":N}
//   loading at start; ready once the project and its startup scene have loaded (a missing or
//   broken startup scene still gives ready, with the reason in the message); failed with the exit
//   code, or with the exception code from the crash handler before its message box shows. Exit
//   code 0 is a normal exit whatever the last state. See launch/LaunchStatusFile.hpp.
//
// Project lock: while a project is open the editor holds <project folder>/.vertexforge/editor.lock
// (launch/ProjectLock.hpp). Switching projects (MCP project_open) moves it. On read-only media the
// project opens without it, with a warning.
namespace
{
    using launch::EditorExitCode;
    using launch::LaunchState;

    // STATUS_STACK_BUFFER_OVERRUN: how abort() ends the process by default (through __fastfail).
    constexpr unsigned long kAbortExceptionCode = 0xC0000409ul;

    std::string joinErrors(const std::vector<std::string>& errors)
    {
        std::string joined;
        for (const std::string& error : errors)
        {
            if (!joined.empty())
            {
                joined += "; ";
            }
            joined += error;
        }
        return joined;
    }

    // Reports why the editor gives up: stderr (for a person or a captured launcher log), the log
    // file, and the status file. Returns the exit code to return from main.
    int fail(EditorExitCode code, const std::string& message)
    {
        std::cerr << "Editor.exe: " << message << std::endl;
        vfLogError("{}", message);
        launch::processLaunchStatus().write(LaunchState::Failed, launch::toInt(code), message);
        return launch::toInt(code);
    }

    // Runs inside the crash filter: no heap, no locks, no throw.
    void onCrash(unsigned long exceptionCode, const char* exceptionLabel, const char* dumpPath) noexcept
    {
        char message[512];
        std::snprintf(message, sizeof(message), "Editor crashed: %s (0x%08lx); crash dump %s",
                      exceptionLabel ? exceptionLabel : "UNKNOWN", exceptionCode, dumpPath ? dumpPath : "");
        launch::processLaunchStatus().writeFromCrash(static_cast<int>(exceptionCode), message);
    }

    // abort() (an uncaught exception, std::terminate, a failed CRT assertion) never reaches the
    // crash filter: it raises SIGABRT, then ends the process through __fastfail.
    void onAbortSignal(int) noexcept
    {
        launch::processLaunchStatus().writeFromCrash(static_cast<int>(kAbortExceptionCode),
                                                     "Editor aborted (abort(), std::terminate or a failed assertion)");
    }
}

int main(int argc, char* argv[]) {
    // Install the crash handler first (so even a fault during log init is dumped), then mirror the
    // log to disk, so that argument errors land in the log too.
    util::installCrashHandler();
    util::initLogFile("Editor");

    std::vector<std::string_view> arguments;
    for (int i = 1; i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }
    const launch::EditorArgumentsResult parsed = launch::parseEditorArguments(arguments);
    const launch::EditorArguments& args = parsed.args;

    // The status file first, so that even bad arguments are reported through it.
    std::vector<std::string> errors = parsed.errors;
    launch::LaunchStatusFile& status = launch::processLaunchStatus();
    if (!args.launchStatusPath.empty())
    {
        if (status.open(std::filesystem::path(args.launchStatusPath)))
        {
            // Only with a status file: a plain start keeps exactly the old crash behaviour.
            util::setCrashNotifier(&onCrash);
            std::signal(SIGABRT, &onAbortSignal);
        }
        else
        {
            errors.push_back("--launch-status " + args.launchStatusPath +
                             ": cannot write a status file there (its folder must exist)");
        }
    }

    if (!errors.empty())
    {
        for (const std::string& error : errors)
        {
            std::cerr << "Editor.exe: " << error << '\n';
        }
        std::cerr << launch::editorUsage() << std::endl;
        const std::string message = "bad arguments: " + joinErrors(errors);
        vfLogError("{}", message);
        status.write(LaunchState::Failed, launch::toInt(EditorExitCode::BadArguments), message);
        return launch::toInt(EditorExitCode::BadArguments);
    }

    // Narrow, as the engine takes path strings in the ANSI code page (VK-1684); messages are UTF-8.
    std::string projectPath;
    std::string projectText;
    bool projectPathUnusable = false;
    if (!args.projectPath.empty())
    {
        try
        {
            std::error_code ec;
            std::filesystem::path projectFile = std::filesystem::absolute(std::filesystem::path(args.projectPath), ec);
            if (ec)
            {
                projectFile = std::filesystem::path(args.projectPath);
            }
            projectFile = projectFile.lexically_normal();
            projectText = launch::pathToUtf8(projectFile);
            // Throws when the absolute path (e.g. through the working directory) has characters the
            // ANSI code page cannot represent.
            projectPath = projectFile.string();
        }
        catch (const std::exception&)
        {
            projectPathUnusable = true;
            if (projectText.empty())
            {
                projectText = args.projectPath;  // the status writer replaces bytes that are not UTF-8
            }
        }
    }

    if (status.isOpen() &&
        !status.write(LaunchState::Loading, 0,
                      args.projectPath.empty() ? std::string("Starting the editor") : "Loading project " + projectText))
    {
        std::cerr << "Editor.exe: --launch-status " << args.launchStatusPath << ": cannot write the status file"
                  << std::endl;
        return launch::toInt(EditorExitCode::BadArguments);
    }

    if (projectPathUnusable)
    {
        return fail(EditorExitCode::ProjectLoadFailed,
                    "cannot open project " + projectText + ": the path has characters this system's code page "
                    "cannot represent");
    }

    // Declared before the editor, so the lock is released last: after cleanUp() and ~EditorHandler,
    // once nothing writes into the project any more.
    launch::ProjectLockKeeper projectLock;

    // Checked before the splash and before EditorHandler exists: its constructor opens the main window.
    if (!projectPath.empty())
    {
        const serialization::ProjectInspection inspection =
            serialization::ProjectSerialization::inspectProject(projectPath);
        if (!inspection.ok())
        {
            const EditorExitCode code = inspection.status == serialization::ProjectInspectionStatus::SchemaTooNew
                                            ? EditorExitCode::ProjectSchemaTooNew
                                            : EditorExitCode::ProjectLoadFailed;
            return fail(code, "cannot open project " + projectText + ": " + inspection.message);
        }

        const launch::ProjectLockKeeper::Result lock = projectLock.switchTo(std::filesystem::path(projectPath));
        if (lock.outcome == launch::ProjectLockKeeper::Result::Outcome::HeldByOther)
        {
            return fail(EditorExitCode::ProjectAlreadyOpen, "cannot open project " + projectText + ": " + lock.message);
        }
        if (lock.outcome == launch::ProjectLockKeeper::Result::Outcome::Unavailable)
        {
            std::cerr << "Editor.exe: warning: opening " << projectText
                      << " without protection against a second editor: " << lock.message << std::endl;
            vfLogWarning("Opening {} without the editor lock: {}", projectText, lock.message);
        }
    }

    editor::SplashScreen::instance().show();

    handlers::EditorHandler editorHandler;

    editor::McpLaunchOptions mcpOptions;
    mcpOptions.enable = args.mcpEnable;
    mcpOptions.port = args.mcpPort;
    mcpOptions.token = args.mcpToken;
    editorHandler.setMcpLaunchOptions(mcpOptions);

    // Every later project switch (MCP project_open) moves the lock; the startup project is already held.
    editorHandler.setProjectOpenGuard(
        [&projectLock](const std::filesystem::path& projectFile) -> std::optional<std::string>
        {
            const launch::ProjectLockKeeper::Result result = projectLock.switchTo(projectFile);
            if (result.outcome == launch::ProjectLockKeeper::Result::Outcome::HeldByOther)
            {
                return result.message;
            }
            if (result.outcome == launch::ProjectLockKeeper::Result::Outcome::Unavailable)
            {
                vfLogWarning("Opening {} without the editor lock: {}", launch::pathToUtf8(projectFile), result.message);
            }
            return std::nullopt;
        });

    editorHandler.init();

    if (projectPath.empty())
    {
        editor::SplashScreen::instance().close();
        status.write(LaunchState::Ready, 0, "Editor ready (no project)");
    }
    else
    {
        handlers::EditorHandler::ProjectLoadResult loaded;
        try
        {
            loaded = editorHandler.loadProject(projectPath);
        }
        catch (const std::exception& e)
        {
            vfLogError("Loading project {} threw: {}", projectPath, e.what());
        }

        if (!loaded.loaded)
        {
            editor::SplashScreen::instance().close();
            const int code = fail(EditorExitCode::ProjectLoadFailed,
                                  "cannot open project " + projectText + ": it failed to load (see logs/Editor.log)");
            editorHandler.cleanUp();
            return code;
        }

        // The splash stays up until the startup scene has loaded (first frame); then ready.
        editorHandler.watchStartup(loaded.warning,
                                   [&status](const handlers::EditorHandler::StartupOutcome& outcome)
                                   {
                                       status.write(LaunchState::Ready, 0, outcome.message);
                                   });
    }

    editorHandler.run();
    // The window can close before the startup scene finished loading.
    editor::SplashScreen::instance().close();
    editorHandler.cleanUp();
    return 0;
}
