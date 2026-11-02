#include <doctest.h>

#include "test_repo_scan_helpers.hpp"

#include <export/ShaderCompiler.hpp>
#include <render/postprocess/effects/AutoExposureEffect.hpp>
#include <resource/ShaderResource.hpp>
#include <resource/Types.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>

// VK-1664: auto exposure adapts as ONE chain. The reduce reads and writes AutoExposureBuffer::state and stores the
// result in the readback slot the CPU reads MAX_FRAMES_IN_FLIGHT frames later; the reduce descriptor set is written
// once. These checks pin the C++ / GLSL layout contract and the preRecord structure the GPU behaviour relies on.

namespace
{
    namespace fs = std::filesystem;

    fs::path repoRoot()
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        return *root;
    }

    fs::path reduceShaderPath()
    {
        return repoRoot() / "resources" / "shaders" / "postprocess" / "auto_exposure_reduce.glsl";
    }

    // Comment-free source with every whitespace character removed, so the checks survive reformatting.
    std::string compactCode(const fs::path& path)
    {
        REQUIRE_MESSAGE(fs::exists(path), "missing " << path.string());
        std::string code = repo_scan::stripComments(repo_scan::readFile(path));
        std::erase_if(code, [](unsigned char c) { return std::isspace(c) != 0; });
        return code;
    }
}

TEST_SUITE("AutoExposure")
{
    TEST_CASE("the exposure buffer and reduce push constants match auto_exposure_reduce.glsl (std430)")
    {
        using namespace render::postprocess;

        CHECK(sizeof(AutoExposureState) == 16);
        CHECK(offsetof(AutoExposureBuffer, state) == 0);
        CHECK(offsetof(AutoExposureBuffer, readback) == 16);
        CHECK(sizeof(AutoExposureBuffer) == 48);
        CHECK(offsetof(AutoExposureReducePushConstants, pixelCount) == 40);
        CHECK(offsetof(AutoExposureReducePushConstants, readbackSlot) == 44);
        CHECK(sizeof(AutoExposureReducePushConstants) == 48);

        const std::string glsl = compactCode(reduceShaderPath());
        CHECK(glsl.find("structExposureState{floatcurrentExposure;floattargetExposure;floatavgLuminance;floatpadding;};")
              != std::string::npos);
        CHECK(glsl.find("bufferExposure{ExposureStatestate;ExposureStatereadback[2];}exposure;") != std::string::npos);
        // readbackSlot is the push_constant block's last member, right after pixelCount (offset 44).
        CHECK(glsl.find("uintpixelCount;uintreadbackSlot;}pc;") != std::string::npos);
    }

    TEST_CASE("the reduce adapts ONE chain and writes this frame's readback slot")
    {
        const std::string glsl = compactCode(reduceShaderPath());

        CHECK(glsl.find("floatprevExposure=exposure.state.currentExposure;") != std::string::npos);
        // NaN-safe: the one persistent chain must never stay stuck on a bad value.
        CHECK(glsl.find("if(!(prevExposure>0.0)||isinf(prevExposure))prevExposure=1.0;") != std::string::npos);
        CHECK(glsl.find("exposure.state=result;") != std::string::npos);
        CHECK(glsl.find("exposure.readback[pc.readbackSlot]=result;") != std::string::npos);
    }

    TEST_CASE("preRecord reads the fence-safe slot, orders the previous frame and never rewrites descriptors")
    {
        const std::string code = compactCode(repoRoot() / "VFEngine" / "graphics" / "render" / "postprocess"
                                             / "effects" / "AutoExposureEffect.cpp");
        const auto begin = code.find("voidAutoExposureEffect::preRecord(");
        const auto end = code.find("voidAutoExposureEffect::record(");
        REQUIRE(begin != std::string::npos);
        REQUIRE(end != std::string::npos);
        REQUIRE(begin < end);
        const std::string body = code.substr(begin, end - begin);

        // The CPU reads the slot this frame writes: written MAX_FRAMES_IN_FLIGHT frames ago, so its submission is done.
        CHECK(body.find("sanitizeExposure(exposureMapped->readback[currentExposureFrame].currentExposure)")
              != std::string::npos);
        CHECK(body.find("readbackSlot=currentExposureFrame;") != std::string::npos);

        // The previous frame's command buffer may still be pending: the reduce set is written once, at creation.
        CHECK(body.find("updateDescriptorSets") == std::string::npos);
        CHECK(body.find("ReduceDescriptorSet(") == std::string::npos);
        CHECK(code.find("writeReduceDescriptorSet();") != std::string::npos);

        // The previous frame's histogram atomics and reduce are ordered before this frame clears the histogram.
        const auto barrier = body.find("pipelineBarrier(");
        const auto fill = body.find("fillBuffer(");
        REQUIRE(barrier != std::string::npos);
        REQUIRE(fill != std::string::npos);
        CHECK(barrier < fill);
    }

    TEST_CASE("auto_exposure_reduce.glsl compiles to SPIR-V")
    {
        // The only automated proof the reduce is valid GLSL: a broken compute shader just leaves auto exposure dead.
        const fs::path shaderPath = reduceShaderPath();
        REQUIRE_MESSAGE(fs::exists(shaderPath), "missing " << shaderPath.string());

        const auto stages = resource::ShaderResource::readShaderFile(shaderPath.string());
        REQUIRE_MESSAGE(!stages.empty(), "no #type stages parsed from " << shaderPath.string());
        REQUIRE(stages.size() == 1);
        CHECK(stages[0].type == resource::ShaderType::COMPUTE);

        shaderCompiler::CompileOptions options;
        options.includeBasePath = shaderPath.parent_path();

        const auto spirv = shaderCompiler::compile(
            stages[0].source,
            shaderCompiler::shaderTypeToVulkanStage(static_cast<uint8_t>(stages[0].type)),
            "auto_exposure_reduce.glsl", options);
        CHECK_MESSAGE(!spirv.empty(), "auto_exposure_reduce.glsl failed to compile");
    }
}
