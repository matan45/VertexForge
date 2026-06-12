#include <doctest.h>
#include <asset/AssetReferenceScanner.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ============================================================
// AssetReferenceScanner: find/update/restore round-trips over
// synthetic JSON container assets, including slash-direction
// variants. JobSystem is uninitialized in tests, so scan jobs
// run synchronously at submit time.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    struct TempTree
    {
        fs::path root;

        TempTree(const std::string& name)
        {
            root = fs::temp_directory_path() / name;
            std::error_code ec;
            fs::remove_all(root, ec);
            fs::create_directories(root, ec);
        }

        ~TempTree()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        fs::path writeFile(const std::string& relPath, const std::string& content) const
        {
            fs::path p = root / relPath;
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream file(p);
            file << content;
            return p;
        }
    };

    std::string readFile(const fs::path& path)
    {
        std::ifstream file(path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::string forwardSlashes(const fs::path& path)
    {
        std::string s = path.string();
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    }

    std::string backSlashes(const fs::path& path)
    {
        std::string s = path.string();
        std::replace(s.begin(), s.end(), '/', '\\');
        return s;
    }

    bool contains(const std::vector<std::string>& list, const std::string& value)
    {
        return std::find(list.begin(), list.end(), value) != list.end();
    }
}

TEST_SUITE("AssetReferenceScanner")
{

TEST_CASE("findReferencingFiles matches forward-slash and backslash references")
{
    TempTree tree("vf_refscan_find");

    fs::path texPath = tree.root / "textures" / "wood.vfimage";
    std::string texFwd = forwardSlashes(texPath);
    std::string texBack = backSlashes(texPath);

    // JSON needs escaped backslashes; raw content holds single ones after parse,
    // but the scanner does a plain substring search on the file text, so embed
    // the literal single-backslash form
    auto matFwd = tree.writeFile("materials/fwd.vfmat",
        "{ \"albedoTextureRefPath\": \"" + texFwd + "\" }");
    auto matBack = tree.writeFile("materials/back.vfmat",
        "{ \"albedoTextureRefPath\": \"" + texBack + "\" }");
    auto matOther = tree.writeFile("materials/other.vfmat",
        "{ \"albedoTextureRefPath\": \"somewhere/else.vfimage\" }");
    // Non-container extensions are never scanned
    tree.writeFile("textures/wood.vfimage", "binary");

    SUBCASE("forward-slash query")
    {
        auto result = asset::AssetReferenceScanner::findReferencingFiles(texFwd, tree.root.string());
        CHECK(contains(result.referencingFiles, matFwd.string()));
        CHECK(contains(result.referencingFiles, matBack.string()));
        CHECK_FALSE(contains(result.referencingFiles, matOther.string()));
    }

    SUBCASE("backslash query finds forward-slash content via normalization")
    {
        auto result = asset::AssetReferenceScanner::findReferencingFiles(texBack, tree.root.string());
        CHECK(contains(result.referencingFiles, matFwd.string()));
        CHECK(contains(result.referencingFiles, matBack.string()));
    }
}

TEST_CASE("updateReferences rewrites paths and preserves slash style")
{
    TempTree tree("vf_refscan_update");

    fs::path oldPath = tree.root / "textures" / "old.vfimage";
    fs::path newPath = tree.root / "textures" / "moved" / "new.vfimage";
    std::string oldFwd = forwardSlashes(oldPath);
    std::string newFwd = forwardSlashes(newPath);
    std::string oldBack = backSlashes(oldPath);
    std::string newBack = backSlashes(newPath);

    // nlohmann re-serializes, so write valid JSON with escaped backslashes
    std::string oldBackJson = oldBack;
    size_t pos = 0;
    while ((pos = oldBackJson.find('\\', pos)) != std::string::npos)
    {
        oldBackJson.insert(pos, 1, '\\');
        pos += 2;
    }

    auto matFwd = tree.writeFile("materials/fwd.vfmat",
        "{ \"albedoTextureRefPath\": \"" + oldFwd + "\" }");
    auto matBack = tree.writeFile("materials/back.vfmat",
        "{ \"albedoTextureRefPath\": \"" + oldBackJson + "\" }");
    auto scene = tree.writeFile("scenes/main.vfscene",
        "{ \"entities\": [ { \"meshRefPath\": \"" + oldFwd + "\" } ] }");
    auto untouched = tree.writeFile("materials/other.vfmat",
        "{ \"albedoTextureRefPath\": \"somewhere/else.vfimage\" }");
    std::string untouchedBefore = readFile(untouched);

    auto result = asset::AssetReferenceScanner::updateReferences(oldFwd, newFwd, tree.root.string());

    CHECK(contains(result.updatedFiles, matFwd.string()));
    CHECK(contains(result.updatedFiles, matBack.string()));
    CHECK(contains(result.updatedFiles, scene.string()));
    CHECK(result.failedFiles.empty());

    // Forward-slash references stay forward-slash
    std::string fwdAfter = readFile(matFwd);
    CHECK(fwdAfter.find(newFwd) != std::string::npos);
    CHECK(fwdAfter.find(oldFwd) == std::string::npos);

    // Backslash references stay backslash (JSON-escaped on disk)
    std::string backAfter = readFile(matBack);
    CHECK(backAfter.find(oldBackJson) == std::string::npos);
    CHECK(backAfter.find("new.vfimage") != std::string::npos);
    CHECK(backAfter.find("\\\\") != std::string::npos);

    // Nested array/object references are updated too
    std::string sceneAfter = readFile(scene);
    CHECK(sceneAfter.find(newFwd) != std::string::npos);

    // Files without the reference are not rewritten
    CHECK(readFile(untouched) == untouchedBefore);
}

TEST_CASE("getOriginalContents and restoreOriginalContents round-trip")
{
    TempTree tree("vf_refscan_restore");

    fs::path oldPath = tree.root / "textures" / "old.vfimage";
    fs::path newPath = tree.root / "textures" / "new.vfimage";
    std::string oldFwd = forwardSlashes(oldPath);
    std::string newFwd = forwardSlashes(newPath);

    auto mat = tree.writeFile("materials/wood.vfmat",
        "{ \"albedoTextureRefPath\": \"" + oldFwd + "\" }");
    std::string before = readFile(mat);

    auto scanResult = asset::AssetReferenceScanner::findReferencingFiles(oldFwd, tree.root.string());
    auto originals = asset::AssetReferenceScanner::getOriginalContents(scanResult.referencingFiles);
    REQUIRE(originals.size() == 1);
    CHECK(originals[0].first == mat.string());
    CHECK(originals[0].second == before);

    asset::AssetReferenceScanner::updateReferences(oldFwd, newFwd, tree.root.string());
    CHECK(readFile(mat) != before);

    CHECK(asset::AssetReferenceScanner::restoreOriginalContents(originals));
    CHECK(readFile(mat) == before);
}

}
