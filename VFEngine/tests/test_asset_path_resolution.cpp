#include <doctest.h>
#include <asset/AssetDatabase.hpp>
#include <algorithm>
#include <filesystem>
#include <string>

// ============================================================
// VK-1346: AssetDatabase::resolveAssetPath — project-relative
// asset paths resolve to the canonical normalized absolute key.
//
// AssetDatabase is a process-wide singleton, so each case
// establishes its own precondition (clear() for the no-project
// state, rebuildFromMetaFiles() to seed a project root).
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // Mirror AssetDatabase::normalizePath (private) so expected values
    // match the implementation exactly.
    std::string normalize(const std::string& path)
    {
        std::string n = path;
        std::replace(n.begin(), n.end(), '\\', '/');
        while (!n.empty() && n.back() == '/') n.pop_back();
        return n;
    }
}

TEST_SUITE("AssetPathResolution")
{

TEST_CASE("empty input returns empty")
{
    auto& db = asset::AssetDatabase::instance();
    db.clear();
    CHECK(db.resolveAssetPath("") == "");
}

TEST_CASE("absolute path passes through with separator normalization")
{
    auto& db = asset::AssetDatabase::instance();
    db.clear();   // no project root set; absolute must not depend on it

    CHECK(db.resolveAssetPath("C:/foo/bar.vfMesh") == "C:/foo/bar.vfMesh");
    CHECK(db.resolveAssetPath("C:\\foo\\bar.vfMesh") == "C:/foo/bar.vfMesh");
}

TEST_CASE("relative path with no project loaded falls back to legacy normalize")
{
    auto& db = asset::AssetDatabase::instance();
    db.clear();   // projectRoot empty -> legacy behavior

    CHECK(db.resolveAssetPath("assets/buildings/HQ.vfMesh") == "assets/buildings/HQ.vfMesh");
    CHECK(db.resolveAssetPath("assets\\buildings\\HQ.vfMesh") == "assets/buildings/HQ.vfMesh");
}

TEST_CASE("relative path resolves against the project root")
{
    auto& db = asset::AssetDatabase::instance();

    // Seed a project root via rebuildFromMetaFiles (requires an existing dir).
    fs::path tempRoot = fs::temp_directory_path() / "vf_asset_path_test_root";
    std::error_code ec;
    fs::create_directories(tempRoot, ec);
    REQUIRE_FALSE(ec);

    REQUIRE(db.rebuildFromMetaFiles(tempRoot.string()));

    std::string expected =
        normalize((tempRoot / "assets/buildings/HQ.vfMesh").lexically_normal().string());

    SUBCASE("plain relative path")
    {
        CHECK(db.resolveAssetPath("assets/buildings/HQ.vfMesh") == expected);
    }

    SUBCASE("backslash separators are converted")
    {
        CHECK(db.resolveAssetPath("assets\\buildings\\HQ.vfMesh") == expected);
    }

    SUBCASE("leading ./ is collapsed")
    {
        CHECK(db.resolveAssetPath("./assets/buildings/HQ.vfMesh") == expected);
    }

    SUBCASE("absolute path still ignores the project root")
    {
        CHECK(db.resolveAssetPath("D:/elsewhere/Other.vfMesh") == "D:/elsewhere/Other.vfMesh");
    }

    fs::remove_all(tempRoot, ec);
    db.clear();
}

}
