#pragma once

#include <filesystem>
#include <string_view>

// VK-1682: the per-project folder the editor keeps its own state in, beside the .vfproj.
// It holds the editor lock (ProjectLock.hpp) and, since VK-1684, the editor's Play-mode saves
// (SaveService) and config.json (ConfigService). It is never project content: the content
// browser and the folder tree hide it, the asset migrator and the reference scanner skip it,
// and the game exporter never packs it.
namespace launch
{
    inline constexpr std::wstring_view editorStateDirName = L".vertexforge";
    inline constexpr std::wstring_view editorLockFileName = L"editor.lock";

    // True when `path` names the editor state folder itself (compares the last component only).
    inline bool isEditorStateDirectory(const std::filesystem::path& path)
    {
        return path.filename().native() == editorStateDirName;
    }
}
