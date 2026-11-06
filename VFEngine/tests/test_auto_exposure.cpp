#include <doctest.h>

#include "test_repo_scan_helpers.hpp"

#include <export/ShaderCompiler.hpp>
#include <render/postprocess/ExposureMath.hpp>
#include <render/postprocess/effects/AutoExposureEffect.hpp>
#include <resource/ShaderResource.hpp>
#include <resource/Types.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

// VK-1664: auto exposure adapts as ONE chain. The reduce reads and writes AutoExposureBuffer::state and stores the
// result in the readback slot the CPU reads MAX_FRAMES_IN_FLIGHT frames later; the reduce descriptor set is written
// once. These checks pin the C++ / GLSL layout contract and the preRecord structure the GPU behaviour relies on.
// The adaptation math has CPU twins in ExposureMath.hpp, and string guards keep the GLSL saying the same. They time the
// ONE chain against ln(20) / speed at several frame rates, model the old two-chain scheme to show what it did, and pin
// the histogram fixes (decode with 254, a percentile window at least one pixel wide) and VK-1670's speed selection.
// preRecord adapts over its own steady clock, never the camera time (the Runtime's is scaled gameplay time).
// test_repo_scan_helpers.hpp pulls in <windows.h>: its min / max macros rule out std::min / std::max here.

using render::postprocess::adaptExposure;
using render::postprocess::binLogLuminance;
using render::postprocess::clampAdaptationDelta;
using render::postprocess::exposureHistogramBinCount;
using render::postprocess::exposureHistogramScale;
using render::postprocess::exposureLogLuminanceRange;
using render::postprocess::exposureMinLogLuminance;
using render::postprocess::firstAdaptationStepSeconds;
using render::postprocess::luminanceHistogramBin;
using render::postprocess::maxAdaptationStepSeconds;
using render::postprocess::meteredExposure;
using render::postprocess::percentileAverageLogLuminance;

namespace
{
    namespace fs = std::filesystem;

    fs::path repoRoot()
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        return *root;
    }

    fs::path shaderDir()
    {
        return repoRoot() / "resources" / "shaders" / "postprocess";
    }

    fs::path reduceShaderPath()
    {
        return shaderDir() / "auto_exposure_reduce.glsl";
    }

    fs::path histogramShaderPath()
    {
        return shaderDir() / "auto_exposure_histogram.glsl";
    }

    fs::path effectSourcePath()
    {
        return repoRoot() / "VFEngine" / "graphics" / "render" / "postprocess" / "effects" / "AutoExposureEffect.cpp";
    }

    // Comment-free source with every whitespace character removed, so the checks survive reformatting.
    std::string compactCode(const fs::path& path)
    {
        REQUIRE_MESSAGE(fs::exists(path), "missing " << path.string());
        std::string code = repo_scan::stripComments(repo_scan::readFile(path));
        std::erase_if(code, [](unsigned char c) { return std::isspace(c) != 0; });
        return code;
    }

    // AutoExposureEffect::preRecord's body, out of compactCode(effectSourcePath()).
    std::string preRecordBody(const std::string& code)
    {
        const auto begin = code.find("voidAutoExposureEffect::preRecord(");
        const auto end = code.find("voidAutoExposureEffect::record(");
        REQUIRE(begin != std::string::npos);
        REQUIRE(end != std::string::npos);
        REQUIRE(begin < end);
        return code.substr(begin, end - begin);
    }

    using Histogram = std::array<uint32_t, exposureHistogramBinCount>;

    // Engine defaults (PostProcessTypes.hpp autoExposure): a brightening adapts at 3 / s, a darkening at 1 / s.
    constexpr float defaultSpeedUp = 3.0f;
    constexpr float defaultSpeedDown = 1.0f;
    constexpr float defaultMinExposure = 0.1f;
    constexpr float defaultMaxExposure = 10.0f;

    // ONE AE step of `seconds`, as preRecord and the reduce make it, with the default settings.
    float adaptOneFrame(float previous, float target, float seconds)
    {
        return adaptExposure(previous, target, defaultSpeedUp, defaultSpeedDown, clampAdaptationDelta(seconds),
                             defaultMinExposure, defaultMaxExposure);
    }

    bool within95(float exposure, float from, float to)
    {
        return std::abs(to - exposure) <= 0.05f * std::abs(to - from);
    }

    // Seconds ONE chain takes to cover 95% of from -> to at a fixed frame rate.
    float chainSecondsTo95(float from, float to, float fps)
    {
        const float frame = 1.0f / fps;
        float exposure = from;
        int steps = 0;
        while (!within95(exposure, from, to))
        {
            exposure = adaptOneFrame(exposure, to, frame);
            ++steps;
            REQUIRE(steps < 100000);
        }
        return static_cast<float>(steps) * frame;
    }

    // What the tonemapper shows each frame NOW: preRecord reads the readback slot this frame writes (written two AE
    // steps earlier), then the reduce advances the ONE chain and stores the result in that slot.
    std::vector<float> shownWithOneChain(const std::vector<float>& frames, float from, float to)
    {
        float chain = from;
        std::array<float, 2> readback{from, from};
        std::vector<float> shown;
        for (size_t n = 0; n < frames.size(); ++n)
        {
            shown.push_back(readback[n % 2]);
            chain = adaptOneFrame(chain, to, frames[n]);
            readback[n % 2] = chain;
        }
        return shown;
    }

    // BEFORE VK-1664 (the parent of 9671b886d), with the same step math: frame N's reduce read and wrote
    // exposureFrames[N % 2], i.e. its own value from frame N - 2, and the tonemapper read exposureFrames[(N + 1) % 2],
    // written on frame N - 1.
    std::vector<float> shownWithTwoChains(const std::vector<float>& frames, float from, float to)
    {
        std::array<float, 2> chains{from, from};
        std::vector<float> shown;
        for (size_t n = 0; n < frames.size(); ++n)
        {
            shown.push_back(chains[(n + 1) % 2]);
            chains[n % 2] = adaptOneFrame(chains[n % 2], to, frames[n]);
        }
        return shown;
    }

    // Seconds until the SHOWN exposure is within 95% of from -> to, or -1.
    float shownSecondsTo95(const std::vector<float>& shown, const std::vector<float>& frames, float from, float to)
    {
        float elapsed = 0.0f;
        for (size_t n = 0; n < shown.size(); ++n)
        {
            if (within95(shown[n], from, to))
                return elapsed;
            elapsed += frames[n];
        }
        return -1.0f;
    }

    // Frames on which the shown exposure turns around (up then down, or down then up): visible flicker.
    int directionReversals(const std::vector<float>& shown)
    {
        int reversals = 0;
        for (size_t n = 2; n < shown.size(); ++n)
        {
            if ((shown[n] - shown[n - 1]) * (shown[n - 1] - shown[n - 2]) < 0.0f)
                ++reversals;
        }
        return reversals;
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

        CHECK(glsl.find("adaptExposure(exposure.state.currentExposure,") != std::string::npos);
        // NaN-safe: the one persistent chain must never stay stuck on a bad value.
        CHECK(glsl.find("if(!(previous>0.0)||isinf(previous))previous=1.0;") != std::string::npos);
        CHECK(glsl.find("exposure.state=result;") != std::string::npos);
        CHECK(glsl.find("exposure.readback[pc.readbackSlot]=result;") != std::string::npos);
    }

    TEST_CASE("preRecord reads the fence-safe slot, orders the previous frame and never rewrites descriptors")
    {
        const std::string code = compactCode(effectSourcePath());
        const std::string body = preRecordBody(code);

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

    TEST_CASE("the auto-exposure compute shaders compile to SPIR-V")
    {
        // The only automated proof they are valid GLSL: a broken compute shader just leaves auto exposure dead.
        for (const char* name : {"auto_exposure_histogram.glsl", "auto_exposure_reduce.glsl"})
        {
            CAPTURE(name);
            const fs::path shaderPath = shaderDir() / name;
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
                name, options);
            CHECK_MESSAGE(!spirv.empty(), name << " failed to compile");
        }
    }

    TEST_CASE("time it: ONE chain covers 95% of a change in ln(20) / speed seconds at 30, 60 and 144 fps")
    {
        const float ln20 = std::log(20.0f);
        for (const float fps : {30.0f, 60.0f, 144.0f})
        {
            CAPTURE(fps);
            const float frame = 1.0f / fps;
            // A brightening (the exposure falls 4 -> 1) adapts at adaptSpeedUp, a darkening (1 -> 4) at adaptSpeedDown:
            // about 1 s and 3 s with the defaults. The two interleaved chains took twice as long.
            CHECK(std::abs(chainSecondsTo95(4.0f, 1.0f, fps) - ln20 / defaultSpeedUp) <= frame);
            CHECK(std::abs(chainSecondsTo95(1.0f, 4.0f, fps) - ln20 / defaultSpeedDown) <= frame);
        }
    }

    TEST_CASE("the exposure after one second does not depend on the frame rate, above 1000 fps included")
    {
        const float expected = 4.0f - 3.0f * std::exp(-1.0f); // 1 -> 4 is a darkening: adaptSpeedDown = 1 / s
        for (const int fps : {30, 60, 144, 2000})
        {
            CAPTURE(fps);
            const float frame = 1.0f / static_cast<float>(fps);
            float exposure = 1.0f;
            for (int n = 0; n < fps; ++n)
                exposure = adaptOneFrame(exposure, 4.0f, frame);
            CHECK(exposure == doctest::Approx(expected).epsilon(1e-3));
        }
        // The old 1 ms floor turned each 0.5 ms frame into a 1 ms step: 3.594 after one second at 2000 fps.
        CHECK(clampAdaptationDelta(0.0005f) == 0.0005f);
    }

    TEST_CASE("VK-1670: adaptSpeedUp when the scene got brighter (the exposure falls), adaptSpeedDown when darker")
    {
        const float frame = 1.0f / 60.0f;
        // Brighter: the metered exposure is below the current one.
        CHECK(adaptExposure(4.0f, 1.0f, 3.0f, 1.0f, frame, defaultMinExposure, defaultMaxExposure)
              == doctest::Approx(4.0f - 3.0f * (1.0f - std::exp(-3.0f * frame))));
        // Darker: the metered exposure is above the current one.
        CHECK(adaptExposure(1.0f, 4.0f, 3.0f, 1.0f, frame, defaultMinExposure, defaultMaxExposure)
              == doctest::Approx(1.0f + 3.0f * (1.0f - std::exp(-1.0f * frame))));
        // Converged: nothing to adapt.
        CHECK(adaptExposure(2.0f, 2.0f, 3.0f, 1.0f, frame, defaultMinExposure, defaultMaxExposure) == 2.0f);
    }

    TEST_CASE("a NaN, infinite or non-positive previous exposure restarts the chain from 1")
    {
        const float frame = 1.0f / 60.0f;
        const float fromOne = adaptExposure(1.0f, 4.0f, 3.0f, 1.0f, frame, defaultMinExposure, defaultMaxExposure);
        for (const float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity(), 0.0f, -0.0f, -1.0f})
        {
            CAPTURE(bad);
            CHECK(adaptExposure(bad, 4.0f, 3.0f, 1.0f, frame, defaultMinExposure, defaultMaxExposure) == fromOne);
        }
    }

    TEST_CASE("an adaptation step stays within [minExposure, maxExposure] and never passes the target")
    {
        const float frame = 1.0f / 60.0f;
        CHECK(adaptExposure(12.0f, 5.0f, 3.0f, 1.0f, frame, 0.1f, 10.0f) == 10.0f);
        CHECK(adaptExposure(0.05f, 0.5f, 3.0f, 1.0f, frame, 0.2f, 10.0f) == 0.2f);
        CHECK(adaptExposure(1.0f, 4.0f, 3.0f, 1.0f, maxAdaptationStepSeconds, 0.1f, 10.0f) < 4.0f);
        CHECK(adaptExposure(4.0f, 1.0f, 3.0f, 1.0f, maxAdaptationStepSeconds, 0.1f, 10.0f) > 1.0f);
    }

    TEST_CASE("clampAdaptationDelta keeps real seconds, floors at 0 and caps a hitch at 0.5 s")
    {
        CHECK(maxAdaptationStepSeconds == 0.5f);
        CHECK(firstAdaptationStepSeconds == 0.016f);
        CHECK(clampAdaptationDelta(1.0f / 60.0f) == 1.0f / 60.0f);
        CHECK(clampAdaptationDelta(0.0005f) == 0.0005f);
        CHECK(clampAdaptationDelta(0.0f) == 0.0f);
        CHECK(clampAdaptationDelta(-0.25f) == 0.0f);
        CHECK(clampAdaptationDelta(3.0f) == 0.5f);
        CHECK(clampAdaptationDelta(std::numeric_limits<float>::infinity()) == 0.5f);
        CHECK(clampAdaptationDelta(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    }

    TEST_CASE("the old per-frame buffers ran two interleaved chains: half speed and alternating frames")
    {
        const float ln20 = std::log(20.0f);

        SUBCASE("steady 60 fps: the old display took twice as long and changed only every other frame")
        {
            const float frame = 1.0f / 60.0f;
            const std::vector<float> frames(480, frame);
            const std::vector<float> now = shownWithOneChain(frames, 1.0f, 4.0f);
            const std::vector<float> before = shownWithTwoChains(frames, 1.0f, 4.0f);

            // ONE chain, shown two steps late: within two frames of ln(20) / adaptSpeedDown.
            const float nowSeconds = shownSecondsTo95(now, frames, 1.0f, 4.0f);
            CHECK(nowSeconds >= ln20 - frame);
            CHECK(nowSeconds <= ln20 + 2.0f * frame);
            const float beforeSeconds = shownSecondsTo95(before, frames, 1.0f, 4.0f);
            CHECK(beforeSeconds >= 2.0f * ln20 - frame);
            CHECK(beforeSeconds <= 2.0f * ln20 + 2.0f * frame);

            size_t notRising = 0;
            for (size_t n = 2; n < 120; ++n)
            {
                if (!(now[n] > now[n - 1]))
                    ++notRising;
            }
            CHECK(notRising == 0);

            size_t staircaseBreaks = 0;
            for (size_t n = 1; n + 1 < before.size(); n += 2)
            {
                if (before[n + 1] != before[n])
                    ++staircaseBreaks;
            }
            CHECK(staircaseBreaks == 0);
        }

        SUBCASE("uneven frame pacing: the old display flickered up and down, ONE chain never turns")
        {
            std::vector<float> frames;
            for (int n = 0; n < 240; ++n)
                frames.push_back(n % 2 == 0 ? 1.0f / 120.0f : 1.0f / 40.0f); // 60 fps on average
            const std::vector<float> now = shownWithOneChain(frames, 1.0f, 4.0f);
            const std::vector<float> before = shownWithTwoChains(frames, 1.0f, 4.0f);

            CHECK(directionReversals(now) == 0);
            CHECK(directionReversals(before) > 200);
            const float nowSeconds = shownSecondsTo95(now, frames, 1.0f, 4.0f);
            CHECK(nowSeconds >= ln20 - 1.0f / 40.0f);
            CHECK(nowSeconds <= ln20 + 2.0f / 40.0f);
        }
    }

    TEST_CASE("a histogram bin decodes to the luminance it encodes: 254 both ways")
    {
        CHECK(exposureHistogramScale == 254.0f);

        // Dividing by 255 (the old reduce) breaks this from bin 128 up.
        uint32_t mismatches = 0;
        uint32_t firstMismatch = 0;
        for (uint32_t bin = 1; bin <= 254; ++bin)
        {
            const float luminance = std::exp2(binLogLuminance(bin, exposureMinLogLuminance, exposureLogLuminanceRange));
            if (luminanceHistogramBin(luminance, exposureMinLogLuminance, exposureLogLuminanceRange) != bin)
            {
                if (mismatches++ == 0)
                    firstMismatch = bin;
            }
        }
        CAPTURE(firstMismatch);
        CHECK(mismatches == 0);

        // Black pixels go to bin 0; anything above the range saturates at the top bin, which decodes to +12 EV.
        CHECK(luminanceHistogramBin(0.001f, exposureMinLogLuminance, exposureLogLuminanceRange) == 0);
        CHECK(luminanceHistogramBin(1e9f, exposureMinLogLuminance, exposureLogLuminanceRange) == 254);
        CHECK(binLogLuminance(254, exposureMinLogLuminance, exposureLogLuminanceRange) == doctest::Approx(12.0f));
    }

    TEST_CASE("a uniform scene meters to middle grey within half a histogram bin")
    {
        const float halfBin = 0.5f * exposureLogLuminanceRange / exposureHistogramScale; // 0.0433 EV
        constexpr uint32_t pixels = 1920 * 1080;
        for (const float luminance : {0.002f, 0.05f, 0.18f, 1.0f, 10.0f, 100.0f, 1000.0f, 4000.0f})
        {
            CAPTURE(luminance);
            Histogram bins{};
            bins[luminanceHistogramBin(luminance, exposureMinLogLuminance, exposureLogLuminanceRange)] = pixels;
            const float average = percentileAverageLogLuminance(bins, pixels, 0.1f, 0.9f, exposureMinLogLuminance,
                                                                exposureLogLuminanceRange);
            CHECK(std::abs(average - std::log2(luminance)) <= halfBin + 1e-4f);
            const float metered = meteredExposure(std::exp2(average), 0.0f, 1e-6f, 1e6f);
            CHECK(std::abs(std::log2(metered) - std::log2(0.18f / luminance)) <= halfBin + 1e-4f);
        }
    }

    TEST_CASE("a zero-width percentile window meters the median instead of jumping to maxExposure")
    {
        Histogram bins{};
        bins[60] = 1000;
        bins[87] = 1000;
        bins[120] = 1000;
        const auto average = [&bins](float low, float high)
        {
            return percentileAverageLogLuminance(bins, 3000, low, high, exposureMinLogLuminance,
                                                 exposureLogLuminanceRange);
        };
        const auto decoded = [](uint32_t bin)
        {
            return binLogLuminance(bin, exposureMinLogLuminance, exposureLogLuminanceRange);
        };

        // low == high (the loader and the editor allow 0.5 / 0.5): a window one pixel wide, at the median. Before
        // VK-1664 every contribution was 0, so the average fell back to -10 EV and the exposure to maxExposure.
        CHECK(average(0.5f, 0.5f) == doctest::Approx(decoded(87)));
        const float medianExposure =
            meteredExposure(std::exp2(average(0.5f, 0.5f)), 0.0f, defaultMinExposure, defaultMaxExposure);
        CHECK(medianExposure == doctest::Approx(0.18f / std::exp2(decoded(87))));
        CHECK(medianExposure < defaultMaxExposure);
        CHECK(average(0.5f, 0.4f) == doctest::Approx(decoded(87)));

        // The default 10% .. 90% window drops 300 pixels at each end.
        CHECK(average(0.1f, 0.9f)
              == doctest::Approx((700.0f * decoded(60) + 1000.0f * decoded(87) + 700.0f * decoded(120)) / 2400.0f));

        // An empty histogram falls back to the bottom of the range.
        CHECK(percentileAverageLogLuminance(Histogram{}, 0, 0.1f, 0.9f, exposureMinLogLuminance,
                                            exposureLogLuminanceRange) == exposureMinLogLuminance);
    }

    TEST_CASE("the auto-exposure shaders state the math ExposureMath.hpp implements")
    {
        const std::string reduce = compactCode(reduceShaderPath());
        CHECK(reduce.find("floatbinLogLuminance(uintbin,floatminLogLuminance,floatlogLuminanceRange){"
                          "return(float(bin)/254.0)*logLuminanceRange+minLogLuminance;}") != std::string::npos);
        CHECK(reduce.find("floatmeteredExposure(floatavgLuminance,floatexposureCompensation,floatminExposure,"
                          "floatmaxExposure){floattarget=0.18/max(avgLuminance,0.001)*exp2(exposureCompensation);"
                          "returnclamp(target,minExposure,maxExposure);}") != std::string::npos);
        CHECK(reduce.find("floatadaptExposure(floatprevious,floattarget,floatspeedUp,floatspeedDown,floatdeltaTime,"
                          "floatminExposure,floatmaxExposure){if(!(previous>0.0)||isinf(previous))previous=1.0;"
                          "floatspeed=(target<previous)?speedUp:speedDown;"
                          "floatadapted=previous+(target-previous)*(1.0-exp(-deltaTime*speed));"
                          "returnclamp(adapted,minExposure,maxExposure);}") != std::string::npos);
        // S1: the percentile window is at least one pixel wide; the loop is percentileAverageLogLuminance's.
        CHECK(reduce.find("floatlowCount=totalPixels*pc.lowPercentile;"
                          "floathighCount=max(totalPixels*pc.highPercentile,lowCount+1.0);") != std::string::npos);
        CHECK(reduce.find("if(runningCount>lowCount&&prevRunning<highCount){floatcontribution=binCount;"
                          "if(prevRunning<lowCount)contribution-=(lowCount-prevRunning);"
                          "if(runningCount>highCount)contribution-=(runningCount-highCount);"
                          "weightedLogSum+=binLogLuminance(i,pc.minLogLuminance,pc.logLuminanceRange)*contribution;"
                          "sum+=contribution;}") != std::string::npos);
        CHECK(reduce.find("floatavgLogLum=(sum>0.0)?(weightedLogSum/sum):pc.minLogLuminance;"
                          "floatavgLum=exp2(avgLogLum);"
                          "floattargetExp=meteredExposure(avgLum,pc.exposureCompensation,pc.minExposure,"
                          "pc.maxExposure);"
                          "floatadaptedExposure=adaptExposure(exposure.state.currentExposure,targetExp,"
                          "pc.adaptSpeedUp,pc.adaptSpeedDown,pc.deltaTime,pc.minExposure,pc.maxExposure);")
              != std::string::npos);
        // S2: nothing decodes with 255 any more.
        CHECK(reduce.find("255.0") == std::string::npos);

        // The histogram's binning, which luminanceHistogramBin mirrors (254 = exposureHistogramScale).
        const std::string histogram = compactCode(histogramShaderPath());
        CHECK(histogram.find("if(luminance>0.001){floatlogLum=clamp((log2(luminance)-pc.minLogLuminance)/"
                             "pc.logLuminanceRange,0.0,1.0);uintbin=uint(logLum*254.0+0.5);bin=clamp(bin,1u,255u);")
              != std::string::npos);
    }

    TEST_CASE("preRecord adapts over real time: its own steady clock, sampled after the early return")
    {
        const std::string body = preRecordBody(compactCode(effectSourcePath()));

        // Never the camera time: the Runtime's is SCALED gameplay time (VK-992), which stalled AE under pause / freeze.
        CHECK(body.find("getCameraData()") == std::string::npos);
        const auto earlyReturn = body.find("if(imageIndex>=histogramDescriptorSets.size())return;");
        const auto clock = body.find("std::chrono::steady_clock::now()");
        REQUIRE(earlyReturn != std::string::npos);
        REQUIRE(clock != std::string::npos);
        CHECK(earlyReturn < clock);
        CHECK(body.find("reducePC.deltaTime=clampAdaptationDelta(stepSeconds);") != std::string::npos);
        // Both shaders get the log luminance range the CPU twins use.
        CHECK(body.find("histPC.minLogLuminance=exposureMinLogLuminance;") != std::string::npos);
        CHECK(body.find("reducePC.minLogLuminance=exposureMinLogLuminance;") != std::string::npos);
    }

    TEST_CASE("VK-1670: the speed settings are described as rates per second, the right way round")
    {
        const std::string editor = compactCode(repoRoot() / "VFEngine" / "editor" / "windows" / "config"
                                               / "PostProcessConfigWindow.cpp");
        CHECK(editor.find("EV/sec") == std::string::npos);
        CHECK(editor.find("\"Adaptationratewhenthescenegetsbrighter(theexposurefalls)") != std::string::npos);
        CHECK(editor.find("\"Adaptationratewhenthescenegetsdarker(theexposurerises)") != std::string::npos);

        const std::string mcp = compactCode(repoRoot() / "VFEngine" / "mcp" / "tools" / "RenderSettingsToolSupport.cpp");
        CHECK(mcp.find("EVpersecond") == std::string::npos);
        CHECK(mcp.find("\"Adaptationratewhenthescenegetsbrighter(theexposurefalls)") != std::string::npos);
        CHECK(mcp.find("\"Adaptationratewhenthescenegetsdarker(theexposurerises)") != std::string::npos);
    }
}
