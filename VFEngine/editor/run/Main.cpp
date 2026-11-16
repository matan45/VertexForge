#include "../handlers/EditorHandler.hpp"
#include "../splash/SplashScreen.hpp"
#include "crash/CrashHandler.hpp"
#include "launch/EditorArguments.hpp"
#include "launch/EditorConsole.hpp"
#include "launch/EditorExitCode.hpp"
#include "launch/LaunchStatusFile.hpp"
#include "launch/ProcessCodePage.hpp"
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

// Editor.exe [project.vfproj] [--launch-status <file>] [--mcp] [--mcp-port <n>] [--mcp-token <t>] [--console]
//
// The first non-flag argument is the project path; flags may appear in any order. Without
// arguments the editor starts with no project.
//
// Launch contract (VK-1682, VK-1683, VK-1684), relied on by the launcher. engine.json carries its revision
// as `launchContract` (launch::editorLaunchContract), so a launcher passes only flags and argument values
// this editor accepts:
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
//
// Output (VK-1683): stdout and stderr redirected to a file or a pipe are unbuffered, so a launcher's
// log is complete up to the last line, even after a crash.
//
// --console (VK-1683): for a launcher that captures the output and starts the editor without a
// console window. The editor opens a console window of its own and mirrors the redirected output
// into it, from the start of the file; the output still goes to the file. Closing that window ends
// the editor (exit code 0xC000013A, as when the console of an editor started from Explorer is
// closed); Ctrl+C is ignored. When stdout already is a console the flag does nothing; when the
// window cannot be opened or cannot show the output, the editor warns and runs on. See
// launch/EditorConsole.hpp.
//
// Unicode (VK-1684): Editor.exe embeds the UTF-8 code page manifest (launch/Utf8CodePage.manifest), so
// its arguments, like every narrow string in the process, are UTF-8 and a path may hold any character.
// That needs Windows 10 version 1903 or later: older Windows passes them in the ANSI code page, and the
// editor logs an error at start. See launch/ProcessCodePage.hpp.
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
    // Before any output: a redirected stdout/stderr must not sit in a CRT buffer (VK-1683).
    launch::prepareStandardStreams();
    // A console shows the UTF-8 output as UTF-8 (VK-1684). Declared first, so destroyed last.
    launch::Utf8ConsoleOutput utf8Console;

    // Then the crash handler (so even a fault during log init is dumped), then mirror the log to
    // disk, so that argument errors land in the log too.
    util::installCrashHandler();
    util::initLogFile("Editor");

    if (!launch::processUsesUtf8())
    {
        // An error, not a warning: Release builds keep only errors (util::engineLogsEnabled).
        vfLogError("Editor.exe runs in code page {}, not UTF-8 (Windows 10 version 1903 or later honours its "
                   "UTF-8 manifest): project paths with characters outside that code page cannot be opened",
                   launch::activeCodePage());
    }

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

    // Narrow, as the engine takes path strings in the process code page: UTF-8 under the VK-1684
    // manifest, the ANSI code page on Windows older than 10 1903. Messages are always UTF-8.
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
            // process code page cannot represent: only an unpaired surrogate under UTF-8, any
            // character outside the ANSI code page on older Windows.
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

    // Declared before the project lock and the editor, so it is destroyed after both: the console
    // mirrors the output to the very end, cleanUp() included.
    launch::EditorConsole editorConsole;
    if (args.console)
    {
        // The file name only; projectText is the UTF-8 absolute path (or the raw argument).
        std::string_view projectName = projectText;
        if (const std::size_t slash = projectName.find_last_of("\\/");
            slash != std::string_view::npos && slash + 1 < projectName.size())
        {
            projectName.remove_prefix(slash + 1);
        }
        const std::string title = "VertexForge Editor - " +
                                  (projectName.empty() ? std::string("no project") : std::string(projectName)) +
                                  " (closing this window closes the editor)";
        const launch::EditorConsole::Result opened = editorConsole.open(title);
        if (opened.status == launch::EditorConsole::Result::Status::Failed ||
            opened.status == launch::EditorConsole::Result::Status::OpenedWithoutMirror)
        {
            vfLogWarning("--console: {}", opened.message);
        }
    }

    if (projectPathUnusable)
    {
        return fail(EditorExitCode::ProjectLoadFailed,
                    "cannot open project " + projectText + ": the path has characters this process's code "
                    "page cannot represent (Unicode paths need Windows 10 version 1903 or later)");
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
