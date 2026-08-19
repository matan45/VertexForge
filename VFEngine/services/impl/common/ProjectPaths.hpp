#pragma once

#include <string>

namespace services
{
    // Paths stored in a scene or a .vfworld outlive the machine that authored them, so they are
    // kept relative to the project working directory — the same convention as
    // ProjectConfig::startupScene. The working directory is an absolute authoring path in the
    // editor and the literal "Assets" in an exported game (GameExporter.cpp:1043), which is what
    // makes one stored string resolve to a loose file in dev and to an "Assets/..." .vfpak key in
    // a shipped build (GameExporter.cpp:712-713).

    // Absolute (or CWD-relative) path -> path relative to the working directory.
    // Returned unchanged when no project is open or the path lies outside the working directory.
    std::string toProjectRelativePath(const std::string& path);

    // The inverse: "<workingDirectory>/<stored>". Absolute paths and paths authored with no
    // project open are returned unchanged.
    std::string resolveProjectPath(const std::string& storedPath);
}
