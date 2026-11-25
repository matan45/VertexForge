#pragma once

// VK-1682: the process exit codes of Editor.exe, the launch contract the launcher relies on.
// The Java twin is launcher/src/main/java/com/vertexforge/launcher/service/EditorExitCode.java;
// tests/test_editor_launch.cpp pins that both carry the same values.
//
// The values stay small and far from the NTSTATUS codes a crashed process exits with
// (0xC0000005 and friends, negative as a Java int). One collision remains: in a Debug build
// the CRT's assertion dialog "Abort" button ends the process with _exit(3). A launcher must
// therefore prefer the --launch-status file, which says "failed" with code 3 only when the
// editor itself refused the project.
namespace launch
{
    enum class EditorExitCode : int
    {
        Ok = 0,                  // normal exit
        BadArguments = 2,        // the command line was rejected (unknown flag, missing value, ...)
        ProjectLoadFailed = 3,   // the .vfproj is missing, unreadable or invalid
        ProjectSchemaTooNew = 4, // the .vfproj schema major is newer than this editor's
        ProjectAlreadyOpen = 5   // another editor holds the project's .vertexforge/editor.lock
    };

    constexpr int toInt(EditorExitCode code) noexcept
    {
        return static_cast<int>(code);
    }
}
