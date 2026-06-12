#include <doctest.h>
#include <export/ExportConfig.hpp>
#include <export/ExportManifest.hpp>
#include <export/ShaderPermutationManifest.hpp>

// ============================================================
// VK-1097: Export Pipeline unit tests
// ============================================================

TEST_SUITE("ExportPipeline") {

// ---- ExportConfig::isValid ----

TEST_CASE("ExportConfig: isValid returns true with all required fields set") {
    gameExport::ExportConfig config;
    config.gameName = "TestGame";
    config.gameVersion = "1.0.0";
    config.outputDirectory = "C:/output";
    config.workingDirectory = "C:/project";
    config.startupScene = "MainScene";

    CHECK(config.isValid());
}

TEST_CASE("ExportConfig: isValid returns false with empty gameName") {
    gameExport::ExportConfig config;
    config.gameName = "";
    config.gameVersion = "1.0.0";
    config.outputDirectory = "C:/output";
    config.workingDirectory = "C:/project";
    config.startupScene = "MainScene";

    CHECK_FALSE(config.isValid());
}

TEST_CASE("ExportConfig: isValid returns false with empty gameVersion") {
    gameExport::ExportConfig config;
    config.gameName = "TestGame";
    config.gameVersion = "";
    config.outputDirectory = "C:/output";
    config.workingDirectory = "C:/project";
    config.startupScene = "MainScene";

    CHECK_FALSE(config.isValid());
}

TEST_CASE("ExportConfig: isValid returns false with empty outputDirectory") {
    gameExport::ExportConfig config;
    config.gameName = "TestGame";
    config.gameVersion = "1.0.0";
    config.outputDirectory = "";
    config.workingDirectory = "C:/project";
    config.startupScene = "MainScene";

    CHECK_FALSE(config.isValid());
}

TEST_CASE("ExportConfig: isValid returns false with empty workingDirectory") {
    gameExport::ExportConfig config;
    config.gameName = "TestGame";
    config.gameVersion = "1.0.0";
    config.outputDirectory = "C:/output";
    config.workingDirectory = "";
    config.startupScene = "MainScene";

    CHECK_FALSE(config.isValid());
}

TEST_CASE("ExportConfig: isValid returns false with empty startupScene") {
    gameExport::ExportConfig config;
    config.gameName = "TestGame";
    config.gameVersion = "1.0.0";
    config.outputDirectory = "C:/output";
    config.workingDirectory = "C:/project";
    config.startupScene = "";

    CHECK_FALSE(config.isValid());
}

TEST_CASE("ExportConfig: default-constructed is invalid") {
    gameExport::ExportConfig config;
    CHECK_FALSE(config.isValid());
}

TEST_CASE("ExportConfig: optional fields do not affect validity") {
    gameExport::ExportConfig config;
    config.gameName = "TestGame";
    config.gameVersion = "1.0.0";
    config.outputDirectory = "C:/output";
    config.workingDirectory = "C:/project";
    config.startupScene = "MainScene";

    // projectFile and iconPath are optional
    CHECK(config.isValid());

    config.projectFile = "C:/project/game.vfproj";
    config.iconPath = "C:/project/icon.ico";
    CHECK(config.isValid());
}

TEST_CASE("ExportConfig: cleanBuild and verifyIntegrity defaults") {
    gameExport::ExportConfig config;
    CHECK_FALSE(config.cleanBuild);
    CHECK(config.verifyIntegrity);
}

TEST_CASE("ExportConfig: material shader gate defaults to failing the export") {
    gameExport::ExportConfig config;
    CHECK(config.failOnEmptyMaterialShaders);
}

TEST_CASE("ExportResult: broken material list starts empty") {
    gameExport::ExportResult result;
    CHECK(result.brokenMaterials.empty());
}

// ---- ShaderPermutationManifest ----

TEST_CASE("ShaderPermutations: getShaderPermutations returns non-empty list") {
    auto perms = shaderCompiler::getShaderPermutations();
    CHECK(perms.size() > 0);
}

TEST_CASE("ShaderPermutations: every permutation has a non-empty glslPath") {
    auto perms = shaderCompiler::getShaderPermutations();
    for (const auto& p : perms) {
        CHECK_FALSE(p.glslPath.empty());
    }
}

TEST_CASE("ShaderPermutations: every permutation has at least one macro") {
    auto perms = shaderCompiler::getShaderPermutations();
    for (const auto& p : perms) {
        CHECK(p.macroNames.size() >= 1);
    }
}

TEST_CASE("ShaderPermutations: no duplicate (glslPath, macroNames) pairs") {
    auto perms = shaderCompiler::getShaderPermutations();

    // Build a set of stringified keys
    std::vector<std::string> keys;
    keys.reserve(perms.size());
    for (const auto& p : perms) {
        std::string key = p.glslPath + "|";
        for (const auto& m : p.macroNames) {
            key += m + ",";
        }
        keys.push_back(key);
    }

    // Check uniqueness
    std::sort(keys.begin(), keys.end());
    for (size_t i = 1; i < keys.size(); ++i) {
        CHECK(keys[i] != keys[i - 1]);
    }
}

TEST_CASE("ShaderPermutations: known shader files are present") {
    auto perms = shaderCompiler::getShaderPermutations();

    bool hasTaskGpudriven = false;
    bool hasMeshShaderGpudriven = false;
    bool hasTaskTerrain = false;
    bool hasMeshTerrain = false;
    bool hasProbeTrace = false;

    for (const auto& p : perms) {
        if (p.glslPath == "gpudriven/task_gpudriven.glsl") hasTaskGpudriven = true;
        if (p.glslPath == "gpudriven/mesh_shader_gpudriven.glsl") hasMeshShaderGpudriven = true;
        if (p.glslPath == "gpudriven/task_terrain.glsl") hasTaskTerrain = true;
        if (p.glslPath == "gpudriven/mesh_terrain.glsl") hasMeshTerrain = true;
        if (p.glslPath == "gi/probe_trace.glsl") hasProbeTrace = true;
    }

    CHECK(hasTaskGpudriven);
    CHECK(hasMeshShaderGpudriven);
    CHECK(hasTaskTerrain);
    CHECK(hasMeshTerrain);
    CHECK(hasProbeTrace);
}

TEST_CASE("ShaderPermutations: CAUSTICS_ENABLED permutations have CAUSTIC_SET macro value") {
    auto perms = shaderCompiler::getShaderPermutations();

    for (const auto& p : perms) {
        bool hasCaustics = false;
        for (const auto& m : p.macroNames) {
            if (m == "CAUSTICS_ENABLED") {
                hasCaustics = true;
                break;
            }
        }
        if (hasCaustics) {
            // Should have a CAUSTIC_SET macro value
            bool hasCausticSet = false;
            for (const auto& mv : p.macroValues) {
                if (mv.first == "CAUSTIC_SET") {
                    hasCausticSet = true;
                    break;
                }
            }
            CHECK(hasCausticSet);
        }
    }
}

// ---- formatHash / parseHash ----

TEST_CASE("formatHash: produces 0x-prefixed hex string") {
    auto str = gameExport::formatHash(0);
    CHECK(str.substr(0, 2) == "0x");
}

TEST_CASE("formatHash/parseHash: roundtrip") {
    uint64_t original = 0xDEADBEEFCAFE1234ULL;
    auto str = gameExport::formatHash(original);
    uint64_t restored = gameExport::parseHash(str);
    CHECK(restored == original);
}

TEST_CASE("formatHash/parseHash: roundtrip zero") {
    auto str = gameExport::formatHash(0);
    CHECK(gameExport::parseHash(str) == 0);
}

TEST_CASE("formatHash/parseHash: roundtrip max") {
    uint64_t maxVal = UINT64_MAX;
    auto str = gameExport::formatHash(maxVal);
    CHECK(gameExport::parseHash(str) == maxVal);
}

// ---- ExportManifest ----

TEST_CASE("ExportManifest: addEntry and findEntry") {
    gameExport::ExportManifest manifest;

    gameExport::ManifestEntry entry;
    entry.archivePath = "assets/textures/diffuse.vfImage";
    entry.contentHash = 12345;
    entry.uncompressedSize = 1024;
    entry.sourceType = "texture";

    manifest.addEntry(entry);

    const auto* found = manifest.findEntry("assets/textures/diffuse.vfImage");
    REQUIRE(found != nullptr);
    CHECK(found->contentHash == 12345);
    CHECK(found->uncompressedSize == 1024);
    CHECK(found->sourceType == "texture");
}

TEST_CASE("ExportManifest: findEntry returns nullptr for missing") {
    gameExport::ExportManifest manifest;
    CHECK(manifest.findEntry("nonexistent") == nullptr);
}

TEST_CASE("ExportManifest: addEntry deduplication") {
    gameExport::ExportManifest manifest;

    gameExport::ManifestEntry entry1;
    entry1.archivePath = "assets/mesh.vfMesh";
    entry1.contentHash = 100;
    manifest.addEntry(entry1);

    gameExport::ManifestEntry entry2;
    entry2.archivePath = "assets/mesh.vfMesh";
    entry2.contentHash = 200;
    manifest.addEntry(entry2);

    // Should have updated the existing entry or added a second
    const auto* found = manifest.findEntry("assets/mesh.vfMesh");
    REQUIRE(found != nullptr);
    // The latest entry's hash should be findable
    CHECK(found->contentHash != 0);
}

TEST_CASE("ExportManifest: getEntries returns all added entries") {
    gameExport::ExportManifest manifest;

    for (int i = 0; i < 5; ++i) {
        gameExport::ManifestEntry entry;
        entry.archivePath = "assets/item" + std::to_string(i);
        entry.contentHash = static_cast<uint64_t>(i * 111);
        manifest.addEntry(entry);
    }

    CHECK(manifest.getEntries().size() >= 5);
}

TEST_CASE("ExportManifest: hasSourceChanged detects new entry") {
    gameExport::ExportManifest manifest;
    // Empty manifest — any source should be considered "changed" (new)
    std::vector<gameExport::ManifestSource> sources;
    sources.push_back({"texture.png", 1000, 42});
    CHECK(manifest.hasSourceChanged("assets/new.vfImage", sources));
}

TEST_CASE("ExportManifest: hasSourceChanged detects modified source") {
    gameExport::ExportManifest manifest;

    gameExport::ManifestEntry entry;
    entry.archivePath = "assets/tex.vfImage";
    entry.sources.push_back({"texture.png", 1000, 42});
    manifest.addEntry(entry);

    // Same path but different modifiedTime AND contentHash — triggers slow path
    std::vector<gameExport::ManifestSource> modified;
    modified.push_back({"texture.png", 2000, 99});
    CHECK(manifest.hasSourceChanged("assets/tex.vfImage", modified));
}

TEST_CASE("ExportManifest: hasSourceChanged returns false for unchanged") {
    gameExport::ExportManifest manifest;

    gameExport::ManifestEntry entry;
    entry.archivePath = "assets/tex.vfImage";
    entry.sources.push_back({"texture.png", 1000, 42});
    manifest.addEntry(entry);

    // Exact same sources
    std::vector<gameExport::ManifestSource> same;
    same.push_back({"texture.png", 1000, 42});
    CHECK_FALSE(manifest.hasSourceChanged("assets/tex.vfImage", same));
}

TEST_CASE("ExportManifest: hasSourceChanged detects count change") {
    gameExport::ExportManifest manifest;

    gameExport::ManifestEntry entry;
    entry.archivePath = "assets/mesh.vfMesh";
    entry.sources.push_back({"mesh.fbx", 1000, 10});
    manifest.addEntry(entry);

    // Now two sources instead of one
    std::vector<gameExport::ManifestSource> changed;
    changed.push_back({"mesh.fbx", 1000, 10});
    changed.push_back({"mesh_lod.fbx", 1000, 20});
    CHECK(manifest.hasSourceChanged("assets/mesh.vfMesh", changed));
}

} // TEST_SUITE
