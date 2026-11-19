#include <doctest.h>

#include "launch/ProjectPathRules.hpp"

#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

// ============================================================
// VK-1687: launch/ProjectPathRules.hpp - which paths lie inside a project folder, how a path inside it
// is written relative to it, and where the editor may create a missing startup scene. The MCP path
// sandbox (mcp/tools/PathSandbox.hpp) shares componentEquals / isWithin.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    struct TempRoot
    {
        fs::path path;

        explicit TempRoot(const fs::path& name)
        {
            path = fs::temp_directory_path() / name;
            std::error_code ec;
            fs::remove_all(path, ec);
            fs::create_directories(path);
        }

        ~TempRoot()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }

        TempRoot(const TempRoot&) = delete;
        TempRoot& operator=(const TempRoot&) = delete;
    };

    std::string withTrailingSeparator(const fs::path& path)
    {
        return path.string() + std::string(1, static_cast<char>(fs::path::preferred_separator));
    }

    // The stored form of launch::relativeInside, or "<outside>".
    std::string relativeText(const fs::path& root, const fs::path& path)
    {
        const std::optional<fs::path> relative = launch::relativeInside(root, path);
        return relative ? relative->generic_string() : std::string("<outside>");
    }
}

TEST_SUITE("ProjectPathRules")
{
    TEST_CASE("relativeInside writes paths inside the root relative to it and refuses the rest")
    {
        TempRoot root("VertexForge_ProjectPaths_Proj");
        TempRoot sibling("VertexForge_ProjectPaths_Project2");

        CHECK(relativeText(root.path, root.path / "scenes" / "Main.vfScene") == "scenes/Main.vfScene");
        CHECK(relativeText(root.path, root.path) == ".");
        CHECK(relativeText(root.path, root.path / ".") == ".");
        CHECK(relativeText(root.path, root.path / "a" / ".." / "b") == "b");

        // A sibling whose name merely starts with the root's name is outside (the old Project
        // Settings check was a string prefix test).
        CHECK(relativeText(fs::temp_directory_path() / "VertexForge_ProjectPaths_Proj",
                           sibling.path / "Main.vfScene") == "<outside>");

        // ".." that leaves the root.
        CHECK(relativeText(root.path, root.path / "a" / ".." / ".." / "Elsewhere") == "<outside>");

        // Another drive (whether or not it exists).
        const fs::path otherDrive = root.path.root_name() == fs::path("Q:") ? fs::path("R:/x/Main.vfScene")
                                                                            : fs::path("Q:/x/Main.vfScene");
        CHECK(relativeText(root.path, otherDrive) == "<outside>");

        // An empty root contains nothing.
        CHECK(relativeText(fs::path(), root.path / "Main.vfScene") == "<outside>");
        CHECK_FALSE(launch::liesInside(fs::path(), root.path));
    }

    TEST_CASE("relativeInside ignores case on Windows and keeps non-ASCII names")
    {
        TempRoot root("VertexForge_ProjectPaths_Case");

#ifdef _WIN32
        std::wstring upper = root.path.wstring();
        for (wchar_t& c : upper)
        {
            c = static_cast<wchar_t>(std::towupper(c));
        }
        CHECK(relativeText(fs::path(upper), root.path / "Scenes" / "Main.vfScene") ==
              "Scenes/Main.vfScene");
        // weakly_canonical keeps the case of components that do not exist, so only a
        // case-insensitive comparison matches these.
        CHECK(relativeText(root.path / "NOSUCH", root.path / "nosuch" / "Main.vfScene") == "Main.vfScene");
        CHECK(launch::componentEquals(fs::path(L"Main.VFSCENE"), fs::path(L"main.vfscene")));
#endif

        // Hebrew folder and file names (wide literals: independent of the source encoding).
        const fs::path hebrewFolder = root.path / fs::path(L"\u05E4\u05E8\u05D5\u05D9\u05E7\u05D8");
        fs::create_directories(hebrewFolder);
        const std::optional<fs::path> relative =
            launch::relativeInside(hebrewFolder, hebrewFolder / fs::path(L"\u05E1\u05E6\u05E0\u05D4.vfScene"));
        REQUIRE(relative.has_value());
        CHECK(relative->wstring() == std::wstring(L"\u05E1\u05E6\u05E0\u05D4.vfScene"));
    }

    TEST_CASE("a trailing separator is not a component")
    {
        TempRoot root("VertexForge_ProjectPaths_Trailing");
        const fs::path missing = root.path / "Missing";

        // A folder that does not exist keeps its trailing separator through weakly_canonical.
        CHECK(relativeText(fs::path(withTrailingSeparator(missing)), missing) == ".");
        CHECK(relativeText(fs::path(withTrailingSeparator(missing)), missing / "x" / "Main.vfScene") ==
              "x/Main.vfScene");
        CHECK(relativeText(root.path, fs::path(withTrailingSeparator(missing))) == "Missing");
        CHECK(relativeText(fs::path(withTrailingSeparator(root.path)), root.path) == ".");

        // isWithin on canonical paths: the empty element of the root is skipped without walking
        // the candidate past its end.
        CHECK(launch::isWithin(fs::path("C:\\a\\b\\"), fs::path("C:\\a\\b")));
        CHECK(launch::isWithin(fs::path("C:\\a\\b\\"), fs::path("C:\\a\\b\\c")));
        CHECK_FALSE(launch::isWithin(fs::path("C:\\a\\b\\"), fs::path("C:\\a")));
        CHECK_FALSE(launch::isWithin(fs::path(), fs::path("C:\\a")));
    }

    TEST_CASE("sameFolder")
    {
        TempRoot root("VertexForge_ProjectPaths_Same");
        CHECK(launch::sameFolder(root.path, root.path / "."));
        CHECK(launch::sameFolder(root.path, fs::path(withTrailingSeparator(root.path))));
        CHECK_FALSE(launch::sameFolder(root.path, root.path / "Assets"));
        CHECK_FALSE(launch::sameFolder(fs::path(), fs::path()));
    }

    TEST_CASE("a starter scene is only created as a .vfScene inside the project, outside .vertexforge")
    {
        TempRoot root("VertexForge_ProjectPaths_Starter");
        TempRoot other("VertexForge_ProjectPaths_StarterOther");

        CHECK(launch::starterSceneAllowed(root.path, root.path / "scenes" / "Main.vfScene"));
        CHECK(launch::starterSceneAllowed(root.path, root.path / "Main.VFSCENE"));
        CHECK(launch::starterSceneAllowed(root.path, root.path / "Assets" / "scenes" / "Main.vfScene"));

        CHECK_FALSE(launch::starterSceneAllowed(root.path, root.path / ".vertexforge" / "Main.vfScene"));
        CHECK_FALSE(launch::starterSceneAllowed(root.path, root.path / ".VERTEXFORGE" / "x" / "Main.vfScene"));
        CHECK_FALSE(launch::starterSceneAllowed(root.path, root.path / "scenes" / "Main.txt"));
        CHECK_FALSE(launch::starterSceneAllowed(root.path, root.path / "scenes" / "Main"));
        CHECK_FALSE(launch::starterSceneAllowed(root.path, root.path));
        CHECK_FALSE(launch::starterSceneAllowed(root.path, root.path / ".." / "VertexForge_ProjectPaths_StarterOther" / "Main.vfScene"));
        CHECK_FALSE(launch::starterSceneAllowed(root.path, other.path / "Main.vfScene"));
        CHECK_FALSE(launch::starterSceneAllowed(fs::path(), fs::path("Main.vfScene")));
    }
}
