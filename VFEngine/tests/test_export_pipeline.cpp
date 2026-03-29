#include <doctest.h>
#include <export/ExportConfig.hpp>
#include <export/ShaderPermutationManifest.hpp>

// Note: ExportManifest.hpp requires GameExport DLL linkage (VF_GAMEEXPORT_API).
// We test only header-accessible parts: ExportConfig (no DLL needed) and
// ShaderPermutationManifest (inline functions, no DLL needed).
// formatHash/parseHash are DLL-exported and cannot be tested without linking GameExport.

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

} // TEST_SUITE
