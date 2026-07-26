#include <doctest.h>

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

    // Tests.exe runs from bin/Tests/<Config>/x64/ and resources/ is NOT copied
    // there, so the repo has to be reached from the source location.
    fs::path shaderRoot()
    {
        return fs::path(__FILE__).parent_path() // VFEngine/tests
            .parent_path()                      // VFEngine
            .parent_path()                      // repo root
            / "resources" / "shaders";
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
}
