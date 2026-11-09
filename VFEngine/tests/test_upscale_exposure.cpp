#include <doctest.h>

#include "test_repo_scan_helpers.hpp" // pulls in <windows.h>: its min/max macros rule out numeric_limits<>::min()/max()

#include <render/postprocess/ExposureMath.hpp>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <filesystem>
#include <limits>
#include <string>

// VK-1663: DLSS-SR reads the engine's exposure (useAutoExposure = eFalse) from a 1x1 texel holding the linear
// multiplier the tonemapper applies this frame. UpscaleManager.cpp needs the Streamline SDK, which Tests does not build
// against, so its call sites are pinned as source text next to the CPU-testable sanitiser.

namespace
{
    namespace fs = std::filesystem;

    // Comment-free source with every whitespace character removed, so the checks survive reformatting.
    std::string compactCode(const fs::path& relative)
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        const fs::path path = *root / relative;
        REQUIRE_MESSAGE(fs::exists(path), "missing " << path.string());
        std::string code = repo_scan::stripComments(repo_scan::readFile(path));
        std::erase_if(code, [](unsigned char c) { return std::isspace(c) != 0; });
        return code;
    }

    size_t countOccurrences(const std::string& haystack, const std::string& needle)
    {
        size_t count = 0;
        for (auto at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + needle.size()))
            ++count;
        return count;
    }

    fs::path upscalingDir()
    {
        return fs::path("VFEngine") / "graphics" / "render" / "upscaling";
    }
}

TEST_SUITE("UpscaleExposure")
{
    TEST_CASE("sanitizeExposure keeps finite positive multipliers and neutralises everything else")
    {
        using render::postprocess::sanitizeExposure;

        static_assert(sanitizeExposure(2.0f) == 2.0f);
        static_assert(sanitizeExposure(0.0f) == 1.0f);

        for (const float kept : {FLT_MIN, 1e-3f, 0.18f, 1.0f, 20.0f, FLT_MAX})
        {
            CAPTURE(kept);
            CHECK(sanitizeExposure(kept) == kept);
        }

        for (const float rejected : {0.0f, -0.0f, -1.0f, -FLT_MAX,
                                     std::numeric_limits<float>::denorm_min(),
                                     std::numeric_limits<float>::quiet_NaN(),
                                     std::numeric_limits<float>::infinity(),
                                     -std::numeric_limits<float>::infinity()})
        {
            CAPTURE(rejected);
            CHECK(sanitizeExposure(rejected) == 1.0f);
        }
    }

    TEST_CASE("DLSS-SR uses the exposure texel: useAutoExposure is eFalse, pre-exposure unsent, presets J/K")
    {
        const std::string code = compactCode(upscalingDir() / "UpscaleManager.cpp");

        // Exactly one assignment, and it disables NGX auto exposure (a creation-time flag in Streamline 2.11.1).
        CHECK(countOccurrences(code, "useAutoExposure=") == 1);
        CHECK(code.find("dlssOptions.useAutoExposure=sl::Boolean::eFalse;") != std::string::npos);

        // The colour input is not pre-exposed: DLSS's 1.0 defaults stay.
        CHECK(code.find("preExposure") == std::string::npos);
        CHECK(code.find("exposureScale") == std::string::npos);

        // Only presets J and K read the exposure texel (DLSS SR guide 3.9; preset L always auto-exposes).
        const std::string presetNeedle = "Preset=sl::DLSSPreset::ePreset";
        size_t presets = 0;
        for (auto at = code.find(presetNeedle); at != std::string::npos; at = code.find(presetNeedle, at + 1))
        {
            ++presets;
            const char letter = at + presetNeedle.size() < code.size() ? code[at + presetNeedle.size()] : '\0';
            CAPTURE(letter);
            CHECK((letter == 'J' || letter == 'K'));
        }
        CHECK(presets >= 5);

        CHECK(compactCode(upscalingDir() / "UpscaleManager.hpp").find("preExposure") == std::string::npos);
    }

    TEST_CASE("the exposure texel is the sanitised tonemap exposure, cleared after the previous frame's read")
    {
        const std::string code = compactCode(fs::path("VFEngine") / "graphics" / "render"
                                             / "RenderPassHandlerAsyncCompute.cpp");

        CHECK(code.find("sanitizeExposure(postProcessPipeline?postProcessPipeline->getTonemapExposure():1.0f)")
              != std::string::npos);
        // ImageUtilities' Undefined -> TransferDst transition starts at TOP_OF_PIPE: no wait on the previous frame's
        // upscaler read of the single exposure image.
        CHECK(code.find("transitionImageLayout(commandBuffer,offscreenResources.exposureImage.image") == std::string::npos);
        CHECK(code.find("toClear.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands;") != std::string::npos);
        CHECK(code.find("inputs.preExposure") == std::string::npos);
    }
}
