#include <doctest.h>
#include <config/Config.hpp>
#include <set>
#include <string>

// ============================================================
// FileExtension constants and Config tests
// FileUtils itself (VF_IMPORT_API, exported from Import.dll) is covered
// in test_import_registry.cpp; this file verifies the FileExtension
// constants defined in Config.hpp.
// ============================================================

TEST_SUITE("FileExtensionConstants") {

TEST_CASE("FileExtension::mesh equals vfMesh") {
    CHECK(FileExtension::mesh == "vfMesh");
}

TEST_CASE("FileExtension::audio equals vfAudio") {
    CHECK(FileExtension::audio == "vfAudio");
}

TEST_CASE("FileExtension::textrue equals vfImage") {
    // Note: the field is named "textrue" (typo in source)
    CHECK(FileExtension::textrue == "vfImage");
}

TEST_CASE("FileExtension::hdr equals vfHdr") {
    CHECK(FileExtension::hdr == "vfHdr");
}

TEST_CASE("FileExtension::animation equals vfAnim") {
    CHECK(FileExtension::animation == "vfAnim");
}

TEST_CASE("FileExtension::animator equals vfAnimator") {
    CHECK(FileExtension::animator == "vfAnimator");
}

TEST_CASE("FileExtension::shader equals glsl") {
    CHECK(FileExtension::shader == "glsl");
}

TEST_CASE("FileExtension::prefab equals vfPrefab") {
    CHECK(FileExtension::prefab == "vfPrefab");
}

TEST_CASE("FileExtension::font equals vfFont") {
    CHECK(FileExtension::font == "vfFont");
}

TEST_CASE("FileExtension::project equals vfproj") {
    CHECK(FileExtension::project == "vfproj");
}

TEST_CASE("FileExtension::assetMeta equals vfmeta") {
    CHECK(FileExtension::assetMeta == "vfmeta");
}

TEST_CASE("All FileExtension constants are non-empty") {
    CHECK_FALSE(FileExtension::textrue.empty());
    CHECK_FALSE(FileExtension::hdr.empty());
    CHECK_FALSE(FileExtension::audio.empty());
    CHECK_FALSE(FileExtension::mesh.empty());
    CHECK_FALSE(FileExtension::animation.empty());
    CHECK_FALSE(FileExtension::animator.empty());
    CHECK_FALSE(FileExtension::shader.empty());
    CHECK_FALSE(FileExtension::prefab.empty());
    CHECK_FALSE(FileExtension::font.empty());
    CHECK_FALSE(FileExtension::project.empty());
    CHECK_FALSE(FileExtension::terrainMaterial.empty());
    CHECK_FALSE(FileExtension::terrainWeights.empty());
    CHECK_FALSE(FileExtension::terrain.empty());
    CHECK_FALSE(FileExtension::water.empty());
    CHECK_FALSE(FileExtension::assetMeta.empty());
}

TEST_CASE("No duplicate FileExtension constants") {
    std::set<std::string> extensions;
    extensions.insert(FileExtension::textrue);
    extensions.insert(FileExtension::hdr);
    extensions.insert(FileExtension::audio);
    extensions.insert(FileExtension::mesh);
    extensions.insert(FileExtension::animation);
    extensions.insert(FileExtension::animator);
    extensions.insert(FileExtension::shader);
    extensions.insert(FileExtension::prefab);
    extensions.insert(FileExtension::font);
    extensions.insert(FileExtension::project);
    extensions.insert(FileExtension::terrainMaterial);
    extensions.insert(FileExtension::terrainWeights);
    extensions.insert(FileExtension::terrain);
    extensions.insert(FileExtension::water);
    extensions.insert(FileExtension::assetMeta);

    // 15 constants inserted, all should be unique
    CHECK(extensions.size() == 15);
}

} // TEST_SUITE
