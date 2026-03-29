#include <doctest.h>
#include <config/ProjectConfig.hpp>
#include <config/Config.hpp>

// ============================================================
// VK-1060: ProjectConfig and Config unit tests
// ============================================================

TEST_SUITE("ProjectConfig") {

// ---- ProjectFileVersion ----

TEST_CASE("ProjectFileVersion: equality with same major.minor") {
    config::ProjectFileVersion a{1, 2};
    config::ProjectFileVersion b{1, 2};
    CHECK(a == b);
}

TEST_CASE("ProjectFileVersion: inequality with different versions") {
    config::ProjectFileVersion a{1, 0};
    config::ProjectFileVersion b{1, 1};
    CHECK_FALSE(a == b);
}

TEST_CASE("ProjectFileVersion: less-than across minor versions") {
    config::ProjectFileVersion a{1, 0};
    config::ProjectFileVersion b{1, 1};
    CHECK(a < b);
    CHECK_FALSE(b < a);
}

TEST_CASE("ProjectFileVersion: less-than across major versions") {
    config::ProjectFileVersion a{0, 9};
    config::ProjectFileVersion b{1, 0};
    CHECK(a < b);
    CHECK_FALSE(b < a);
}

TEST_CASE("ProjectFileVersion: toString returns non-empty string") {
    config::ProjectFileVersion v{2, 3};
    std::string str = v.toString();
    CHECK_FALSE(str.empty());
    CHECK(str == "2.3");
}

TEST_CASE("ProjectFileVersion: isCompatible with matching schema major") {
    config::ProjectFileVersion v;
    v.major = config::ProjectSchemaVersion::major;
    v.minor = 99;
    CHECK(v.isCompatible());
}

TEST_CASE("ProjectFileVersion: isCompatible fails with non-matching schema major") {
    config::ProjectFileVersion v;
    v.major = config::ProjectSchemaVersion::major + 1;
    v.minor = 0;
    CHECK_FALSE(v.isCompatible());
}

// ---- ProjectConfig ----

TEST_CASE("ProjectConfig: isValid with all required fields set") {
    config::ProjectConfig cfg;
    cfg.projectName = "TestProject";
    cfg.version = "1.0";
    cfg.workingDirectory = "/some/path";
    cfg.startupScene = "main.scene";
    CHECK(cfg.isValid());
}

TEST_CASE("ProjectConfig: isValid false with empty projectName") {
    config::ProjectConfig cfg;
    cfg.projectName = "";
    cfg.version = "1.0";
    cfg.workingDirectory = "/some/path";
    cfg.startupScene = "main.scene";
    CHECK_FALSE(cfg.isValid());
}

TEST_CASE("ProjectConfig: isValid false with empty version") {
    config::ProjectConfig cfg;
    cfg.projectName = "TestProject";
    cfg.version = "";
    cfg.workingDirectory = "/some/path";
    cfg.startupScene = "main.scene";
    CHECK_FALSE(cfg.isValid());
}

// ---- FileExtension constants ----

TEST_CASE("FileExtension: constants are non-empty strings") {
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
    CHECK_FALSE(FileExtension::svt.empty());
    CHECK_FALSE(FileExtension::assetMeta.empty());
}

// ---- FileVersion defaults ----

TEST_CASE("FileVersion: default matches Version constants") {
    FileVersion fv;
    CHECK(fv.major == Version::major);
    CHECK(fv.minor == Version::minor);
    CHECK(fv.patch == Version::patch);
}

// ---- ProjectSchemaVersion ----

TEST_CASE("ProjectSchemaVersion: toString returns non-empty") {
    std::string str = config::ProjectSchemaVersion::toString();
    CHECK_FALSE(str.empty());
}

} // TEST_SUITE
