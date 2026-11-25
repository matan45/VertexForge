#include <doctest.h>

#include "launch/ProjectPathRules.hpp"
#include "resource/ArchivePathKey.hpp"
#include "serialization/SceneSerialization.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

// ============================================================
// VK-1726: a scene refers to its terrain ("terrainRefPath") and its .vfSettings sidecar relative to its own
// folder. A shipped game's save slot lives in %LOCALAPPDATA%\<Publisher>\<Game>\saves, outside the game folder,
// and the game's terrain is "Assets/..." (relative to the working directory, inside the .vfpak, nothing on disk):
// no relative form exists, and the old fallback wrote "Assets/..." as it was, which the load then joined onto the
// SLOT's folder. The value is now absolute, and the archive maps an absolute path inside the game folder back to
// its key.
//
// Paths under the temp folder may be looked at (fs::relative does); the placeholder paths given to archiveKeyFor
// are only compared, never touched.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // "שמירה" (saving), as UTF-16 and as UTF-8.
    const std::wstring kHebrewWide = L"שמירה";
    const std::string kHebrewUtf8 = "\xD7\xA9\xD7\x9E\xD7\x99\xD7\xA8\xD7\x94";

    // Nothing by this name exists in the working directory, so weakly_canonical leaves the path relative, the way
    // a shipped game's "Assets/..." is (its files live in the .vfpak).
    constexpr const char* kMissingFolder = "VertexForge_SceneRefPaths_NoSuchFolder";

    fs::path tempBase()
    {
        std::error_code ec;
        fs::path base = fs::weakly_canonical(fs::temp_directory_path(ec), ec);
        return base / L"VertexForge_SceneRefPaths";
    }

    fs::path asNative(const std::string& stored)
    {
        return fs::path(stored).lexically_normal();
    }
}

TEST_SUITE("SceneRefPaths")
{
    TEST_CASE("a scene in the game keeps its terrain path relative to its folder")
    {
        // The exported layout (scene and terrain in the .vfpak): unchanged by VK-1726.
        const std::string scene = "Assets/scenes/Main.vfScene";
        const std::string stored = serialization::SceneSerialization::makeSettingsRefPath(
            scene, fs::path("Assets/terrain/x.vfTerrain"));
        CHECK(stored == "../terrain/x.vfTerrain");

        const fs::path resolved = serialization::SceneSerialization::resolveSettingsRefPath(scene, stored);
        CHECK(resolved.generic_string() == "Assets/terrain/x.vfTerrain");
    }

    TEST_CASE("a scene and its terrain in one folder tree keep the relative form")
    {
        // The editor's case: absolute paths in the project (none of them need to exist).
        const fs::path project = tempBase() / L"project";
        const fs::path scene = project / L"scenes" / L"Main.vfScene";
        const fs::path terrain = project / L"terrain" / L"x.vfTerrain";

        const std::string stored = serialization::SceneSerialization::makeSettingsRefPath(scene.string(), terrain);
        CHECK(stored == "../terrain/x.vfTerrain");
        CHECK(serialization::SceneSerialization::resolveSettingsRefPath(scene.string(), stored) ==
              terrain.lexically_normal());
    }

    TEST_CASE("a save slot outside the game folder stores the game's relative terrain path absolute")
    {
        const fs::path scene = tempBase() / L"saves" / L"slot1" / L"scene.vfScene";
        const fs::path terrain = fs::path(kMissingFolder) / "terrain" / "x.vfTerrain";

        const std::string stored = serialization::SceneSerialization::makeSettingsRefPath(scene.string(), terrain);
        const fs::path expected = fs::absolute(terrain).lexically_normal();

        CHECK(fs::path(stored).is_absolute());
        CHECK(asNative(stored) == expected);
        // Read back as it was meant: the same file, not one under the slot's folder.
        CHECK(serialization::SceneSerialization::resolveSettingsRefPath(scene.string(), stored) == expected);
    }

    TEST_CASE("a scene in the working directory keeps a path relative to it as it is")
    {
        // A bare scene file name reads its references back against the working directory, so the relative
        // text already means the right file.
        const fs::path terrain = fs::path(kMissingFolder) / "x.vfTerrain";
        const std::string stored = serialization::SceneSerialization::makeSettingsRefPath("Main.vfScene", terrain);
        CHECK(stored == terrain.generic_string());
        CHECK(serialization::SceneSerialization::resolveSettingsRefPath("Main.vfScene", stored) ==
              terrain.lexically_normal());
    }

    TEST_CASE("an absolute path inside the game folder finds its archive key")
    {
        const fs::path root(L"C:\\Games\\Acme Quest");

        CHECK(resource::archiveKeyFor("C:\\Games\\Acme Quest\\Assets\\terrain\\x.vfTerrain", root) ==
              "Assets/terrain/x.vfTerrain");
        CHECK(resource::archiveKeyFor("C:/Games/Acme Quest/Assets/terrain/x.vfTerrain", root) ==
              "Assets/terrain/x.vfTerrain");
        // Windows compares paths ignoring case.
        CHECK(resource::archiveKeyFor("c:\\games\\ACME QUEST\\Assets\\x.vfTerrain", root) == "Assets/x.vfTerrain");
        // What resolveSettingsRefPath gives for "..\..\..\Acme Quest\Assets\x" from a slot is normalised anyway.
        CHECK(resource::archiveKeyFor("C:\\Games\\Acme Quest\\saves\\..\\Assets\\x.vfTerrain", root) ==
              "Assets/x.vfTerrain");
        // A root spelled with a trailing separator.
        CHECK(resource::archiveKeyFor("C:\\Games\\Acme Quest\\Assets\\x.vfTerrain",
                                      fs::path(L"C:\\Games\\Acme Quest\\")) == "Assets/x.vfTerrain");
        // A network share.
        CHECK(resource::archiveKeyFor("\\\\server\\share\\Game\\Assets\\x.vfTerrain",
                                      fs::path(L"\\\\server\\share\\Game")) == "Assets/x.vfTerrain");
        // A UTF-8 path (Tests.exe runs in the UTF-8 code page, like the Runtime).
        CHECK(resource::archiveKeyFor("C:\\Games\\" + kHebrewUtf8 + "\\Assets\\x.vfTerrain",
                                      fs::path(L"C:\\Games\\") / kHebrewWide) == "Assets/x.vfTerrain");
    }

    TEST_CASE("any other path keeps its own key with forward slashes")
    {
        const fs::path root(L"C:\\Games\\Acme Quest");

        // Relative: exactly as before VK-1726.
        CHECK(resource::archiveKeyFor("Assets\\terrain\\x.vfTerrain", root) == "Assets/terrain/x.vfTerrain");
        CHECK(resource::archiveKeyFor("Assets/terrain/x.vfTerrain", root) == "Assets/terrain/x.vfTerrain");
        CHECK(resource::archiveKeyFor("Assets\\scenes\\..\\x", root) == "Assets/scenes/../x");
        // Outside the root, a folder whose name only starts like it, another drive, the root itself.
        CHECK(resource::archiveKeyFor("C:\\Games\\Other\\Assets\\x", root) == "C:/Games/Other/Assets/x");
        CHECK(resource::archiveKeyFor("C:\\Games\\Acme Quest 2\\Assets\\x", root) == "C:/Games/Acme Quest 2/Assets/x");
        CHECK(resource::archiveKeyFor("D:\\Games\\Acme Quest\\Assets\\x", root) == "D:/Games/Acme Quest/Assets/x");
        CHECK(resource::archiveKeyFor("C:\\Games\\Acme Quest", root) == "C:/Games/Acme Quest");
        // No archive open: no root.
        CHECK(resource::archiveKeyFor("C:\\Games\\Acme Quest\\Assets\\x", std::span<const fs::path>{}) ==
              "C:/Games/Acme Quest/Assets/x");
    }

    TEST_CASE("every archive root is tried in turn")
    {
        // The canonical form of the game folder first, then its spelling (a short name, a junction).
        const std::vector<fs::path> roots{fs::path(L"C:\\Program Files\\Acme Quest"), fs::path(L"C:\\PROGRA~1\\ACMEQU~1")};
        CHECK(resource::archiveKeyFor("C:\\PROGRA~1\\ACMEQU~1\\Assets\\x.vfTerrain", roots) == "Assets/x.vfTerrain");
        CHECK(resource::archiveKeyFor("C:\\Program Files\\Acme Quest\\Assets\\x.vfTerrain", roots) ==
              "Assets/x.vfTerrain");
    }

    TEST_CASE("lexicallyRelativeInside compares components without the disk")
    {
        const fs::path root(L"C:\\Games\\Acme Quest");
        const std::optional<fs::path> inside = launch::lexicallyRelativeInside(root, fs::path(L"C:/Games/acme quest/a/b"));
        REQUIRE(inside.has_value());
        CHECK(inside->generic_string() == "a/b");

        const std::optional<fs::path> itself = launch::lexicallyRelativeInside(root, root);
        REQUIRE(itself.has_value());
        CHECK(*itself == fs::path("."));

        CHECK_FALSE(launch::lexicallyRelativeInside(root, fs::path(L"C:\\Games")).has_value());
        CHECK_FALSE(launch::lexicallyRelativeInside(root, fs::path(L"C:\\Games\\Acme Quest 2\\a")).has_value());
        CHECK_FALSE(launch::lexicallyRelativeInside(fs::path(), fs::path(L"C:\\a")).has_value());
        CHECK_FALSE(launch::lexicallyRelativeInside(root, fs::path()).has_value());
    }
}
