#include <doctest.h>

#include "test_repo_scan_helpers.hpp"

#include <export/ShaderCompiler.hpp>
#include <resource/ShaderResource.hpp>
#include <resource/Types.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ============================================================
// VK-1631: text.glsl and ui_text.glsl share their SDF anti-aliasing through
// resources/shaders/common/text_sdf.glsl.
//
// A broken shader is nearly silent at runtime: Shader::readShader logs and the
// text pipeline simply draws nothing. Nothing else in the suite compiles GLSL,
// so these cases are the only automated guard that
//   (a) `#include "../common/text_sdf.glsl"` still resolves, and
//   (b) both stages of both text shaders still produce SPIR-V.
//
// shaderCompiler::compile is CPU-only (shaderc) - no Vulkan device, no window.
// Its ShaderIncluder mirrors the runtime one in graphics/core/Shader.cpp, so
// include resolution is exercised the same way the editor exercises it.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // Tests.exe runs from bin/Tests/<Config>/x64/ and resources/ is NOT copied there,
    // so the shader tree has to be reached in the repo. Reuses the same locator as the
    // other source-reading suites (test_script_native_parity, test_script_listener_coverage).
    fs::path shaderRoot()
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        return *root / "resources" / "shaders";
    }

    // Compiles every #type stage of one .glsl, with includes resolved relative to
    // the file's own directory (the same base path the engine and the exporter use).
    void requireAllStagesCompile(const fs::path& shaderPath)
    {
        REQUIRE_MESSAGE(fs::exists(shaderPath), "missing " << shaderPath.string());

        const auto stages = resource::ShaderResource::readShaderFile(shaderPath.string());
        REQUIRE_MESSAGE(!stages.empty(), "no #type stages parsed from " << shaderPath.string());

        shaderCompiler::CompileOptions options;
        options.includeBasePath = shaderPath.parent_path();

        bool sawVertex = false;
        bool sawFragment = false;

        for (const auto& stage : stages)
        {
            sawVertex = sawVertex || stage.type == resource::ShaderType::VERTEX;
            sawFragment = sawFragment || stage.type == resource::ShaderType::FRAGMENT;

            const auto vkStage =
                shaderCompiler::shaderTypeToVulkanStage(static_cast<uint8_t>(stage.type));
            const std::string name =
                shaderPath.filename().string() + ":" + std::to_string(static_cast<int>(stage.type));

            // compile() returns an empty vector and logs on failure.
            const auto spirv = shaderCompiler::compile(stage.source, vkStage, name, options);
            CHECK_MESSAGE(!spirv.empty(), "failed to compile " << name);
        }

        CHECK(sawVertex);
        CHECK(sawFragment);
    }

    std::string readShaderSource(const fs::path& path)
    {
        std::ifstream in(path);
        REQUIRE_MESSAGE(in.is_open(), "missing " << path.string());
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }

    size_t countOccurrences(const std::string& haystack, const std::string& needle)
    {
        size_t count = 0;
        for (size_t at = haystack.find(needle); at != std::string::npos;
             at = haystack.find(needle, at + needle.size()))
        {
            ++count;
        }
        return count;
    }
}

TEST_SUITE("TextShaderCompile")
{
    TEST_CASE("the shared SDF include exists and is include-only")
    {
        const fs::path include = shaderRoot() / "common" / "text_sdf.glsl";
        REQUIRE_MESSAGE(fs::exists(include), "missing " << include.string());

        std::ifstream in(include);
        REQUIRE(in.is_open());
        const std::string source((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());

        // Files under common/ are pulled into another translation unit, so they must
        // carry a header guard and must not open a stage or declare #version.
        CHECK(source.find("#ifndef TEXT_SDF_GLSL") != std::string::npos);
        CHECK(source.find("float medianRGB(") != std::string::npos);
        CHECK(source.find("float sdfCoverage(") != std::string::npos);
        CHECK(source.find("fwidth(") != std::string::npos);
        CHECK(source.find("#type ") == std::string::npos);
        CHECK(source.find("#version") == std::string::npos);
    }

    TEST_CASE("ui/ui_text.glsl compiles with the shared SDF include")
    {
        requireAllStagesCompile(shaderRoot() / "ui" / "ui_text.glsl");
    }

    TEST_CASE("text/text.glsl compiles with the shared SDF include")
    {
        requireAllStagesCompile(shaderRoot() / "text" / "text.glsl");
    }

    TEST_CASE("both text shaders route the SDF branch through sdfCoverage")
    {
        // Pins the dedup: neither shader may reintroduce its own smoothstep band.
        for (const fs::path relative : {fs::path("ui") / "ui_text.glsl",
                                        fs::path("text") / "text.glsl"})
        {
            const fs::path shader = shaderRoot() / relative;
            std::ifstream in(shader);
            REQUIRE_MESSAGE(in.is_open(), "missing " << shader.string());
            const std::string source((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());

            const std::string label = relative.generic_string();
            CAPTURE(label);
            CHECK(source.find("#include \"../common/text_sdf.glsl\"") != std::string::npos);
            // Each stage is its own translation unit, so the pragma must sit in the
            // fragment stage too - not just once at the top of the file.
            CHECK(source.find("#extension GL_GOOGLE_include_directive : require")
                  != std::string::npos);
            CHECK(source.find("sdfCoverage(") != std::string::npos);
            CHECK(source.find("smoothstep(") == std::string::npos);
        }
    }

    TEST_CASE("both text shaders reconstruct MTSDF and italicize every non-color mode")
    {
        for (const fs::path relative : {fs::path("ui") / "ui_text.glsl",
                                        fs::path("text") / "text.glsl"})
        {
            const fs::path shader = shaderRoot() / relative;
            std::ifstream in(shader);
            REQUIRE_MESSAGE(in.is_open(), "missing " << shader.string());
            const std::string source((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());

            const std::string label = relative.generic_string();
            CAPTURE(label);
            CHECK(source.find("pc.glyphMode == 2u") != std::string::npos);
            CHECK(source.find("medianRGB(fieldSample.rgb)") != std::string::npos);
            CHECK(source.find("pc.glyphMode != 1u") != std::string::npos);
            CHECK(source.find("pc.glyphMode == 0u") == std::string::npos);
            CHECK(source.find(": fieldSample.r;") != std::string::npos);
        }
    }

    // VK-1634: the MTSDF band is analytic - derived from the bake-time pxRange and the uv
    // varying - rather than from fwidth() of the sampled median. Taking a derivative of the
    // median is exactly what softens the sharp corners the encoding exists to produce, so
    // this case pins the plumbing end to end: the push-constant member that carries pxRange,
    // the two helpers in the shared include, and the call sites in both shaders.
    TEST_CASE("VK-1634: the MTSDF band comes from pxRange, not from a field derivative")
    {
        const std::string include = readShaderSource(shaderRoot() / "common" / "text_sdf.glsl");

        CHECK(include.find("float screenPxRange(") != std::string::npos);
        CHECK(include.find("float sdfCoverageRange(") != std::string::npos);
        // The analytic band differences the uv varying. fwidth() of the uv would be an L1
        // approximation, which italic shear and world-space camera roll would expose, so the
        // L2 dFdx/dFdy pair is load-bearing rather than stylistic.
        CHECK(include.find("dFdx(") != std::string::npos);
        CHECK(include.find("dFdy(") != std::string::npos);
        // ...and the legacy fwidth() band must survive untouched beside it. Note
        // "sdfCoverageRange(" does NOT contain "sdfCoverage(", so this still pins the
        // original function rather than matching the new one by accident.
        CHECK(include.find("float sdfCoverage(") != std::string::npos);
        CHECK(include.find("fwidth(") != std::string::npos);

        for (const fs::path relative : {fs::path("ui") / "ui_text.glsl",
                                        fs::path("text") / "text.glsl"})
        {
            const std::string source = readShaderSource(shaderRoot() / relative);
            const std::string label = relative.generic_string();
            CAPTURE(label);

            // pxRange occupies what used to be dead padding at offset 12. Both stages share
            // one push-constant range, so both blocks must declare it or the layouts diverge
            // - and that divergence is silent, since nothing else cross-checks them.
            CHECK(countOccurrences(source, "float pxRange;") == 2);
            CHECK(source.find("float padding") == std::string::npos);
            CHECK(source.find("pc.pxRange") != std::string::npos);

            // Derivatives on the uv varying, and the atlas size straight from the sampler -
            // the shared include must never name fontAtlas itself, because text.glsl binds
            // it at 1 and ui_text.glsl at 0.
            CHECK(source.find("screenPxRange(fragTexCoord") != std::string::npos);
            CHECK(source.find("textureSize(fontAtlas, 0)") != std::string::npos);
            CHECK(source.find("sdfCoverageRange(") != std::string::npos);
            // The legacy call must still be there for glyphMode 0.
            CHECK(source.find("sdfCoverage(sdfValue") != std::string::npos);
        }

        // The include is shared, so it must stay sampler-agnostic.
        CHECK(include.find("fontAtlas") == std::string::npos);
    }
}
