#pragma once

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>

namespace render::postprocess
{
    // VK-1663: a LINEAR exposure multiplier that is safe to hand on: the DLSS exposure texel (DLSS-SR reads it with
    // useAutoExposure = eFalse) and the auto-exposure readback. NaN, +/-Inf, zero, negative and subnormal values become
    // the neutral 1. Streamline forwards the texel to DLSS unchecked, and a bad value would persist in its history.
    // Comparisons with NaN are false under the project's default /fp:precise; the lower bound is FLT_MIN rather than 0
    // because 1/subnormal overflows to Inf.
    [[nodiscard]] constexpr float sanitizeExposure(float exposure) noexcept
    {
        return exposure >= FLT_MIN && exposure <= FLT_MAX ? exposure : 1.0f;
    }

    // VK-1664: the auto-exposure math, header-only and Vulkan-free so the CPU-only Tests project (test_auto_exposure)
    // can time the adaptation. luminanceHistogramBin is the CPU twin of auto_exposure_histogram.glsl's binning;
    // binLogLuminance, meteredExposure and adaptExposure are the twins of the same-named functions in
    // auto_exposure_reduce.glsl, and percentileAverageLogLuminance of its percentile loop. Keep each statement for
    // statement identical to its GLSL twin. <windows.h> (min / max macros) precedes this header in Tests, so GLSL's
    // clamp(x, lo, hi) = min(max(x, lo), hi) is written std::fmin(std::fmax(x, lo), hi).

    // The histogram's log2 luminance range, -10 .. +12 EV; AutoExposureEffect pushes it to both shaders.
    inline constexpr float exposureMinLogLuminance = -10.0f;
    inline constexpr float exposureLogLuminanceRange = 22.0f;
    inline constexpr uint32_t exposureHistogramBinCount = 256;
    // The histogram stores round(t * 254) for the normalised log luminance t (bins 1..254; bin 0 holds the black
    // pixels), and the reduce decodes with the same 254.
    inline constexpr float exposureHistogramScale = 254.0f;

    // The bin auto_exposure_histogram.glsl counts a pixel of this luminance in.
    [[nodiscard]] inline uint32_t luminanceHistogramBin(float luminance, float minLogLuminance,
                                                        float logLuminanceRange) noexcept
    {
        if (!(luminance > 0.001f))
            return 0;
        const float logLum =
            std::fmin(std::fmax((std::log2(luminance) - minLogLuminance) / logLuminanceRange, 0.0f), 1.0f);
        const auto bin = static_cast<uint32_t>(logLum * exposureHistogramScale + 0.5f);
        return std::clamp(bin, uint32_t{1}, uint32_t{255});
    }

    // The log2 luminance at the centre of a bin.
    [[nodiscard]] inline float binLogLuminance(uint32_t bin, float minLogLuminance, float logLuminanceRange) noexcept
    {
        return (static_cast<float>(bin) / exposureHistogramScale) * logLuminanceRange + minLogLuminance;
    }

    // The mean log2 luminance of the pixels between the low and high percentiles. The window is at least one pixel
    // wide, so low == high meters the median; an empty histogram falls back to minLogLuminance.
    [[nodiscard]] inline float percentileAverageLogLuminance(
        const std::array<uint32_t, exposureHistogramBinCount>& bins, uint32_t pixelCount, float lowPercentile,
        float highPercentile, float minLogLuminance, float logLuminanceRange) noexcept
    {
        const float totalPixels = static_cast<float>(pixelCount);
        const float lowCount = totalPixels * lowPercentile;
        const float highCount = std::fmax(totalPixels * highPercentile, lowCount + 1.0f);

        float sum = 0.0f;
        float weightedLogSum = 0.0f;
        float runningCount = 0.0f;
        for (uint32_t i = 0; i < exposureHistogramBinCount; ++i)
        {
            const float binCount = static_cast<float>(bins[i]);
            const float prevRunning = runningCount;
            runningCount += binCount;

            if (runningCount > lowCount && prevRunning < highCount)
            {
                float contribution = binCount;
                if (prevRunning < lowCount)
                    contribution -= (lowCount - prevRunning);
                if (runningCount > highCount)
                    contribution -= (runningCount - highCount);

                weightedLogSum += binLogLuminance(i, minLogLuminance, logLuminanceRange) * contribution;
                sum += contribution;
            }
        }
        return sum > 0.0f ? weightedLogSum / sum : minLogLuminance;
    }

    // The multiplier that brings the average luminance to middle grey (0.18), shifted by the compensation (EV) and
    // clamped.
    [[nodiscard]] inline float meteredExposure(float avgLuminance, float exposureCompensation, float minExposure,
                                               float maxExposure) noexcept
    {
        const float target = 0.18f / std::fmax(avgLuminance, 0.001f) * std::exp2(exposureCompensation);
        return std::fmin(std::fmax(target, minExposure), maxExposure);
    }

    // One step of the ONE adaptation chain over deltaTime seconds: it covers 1 - exp(-deltaTime * speed) of the
    // remaining distance, so steps compose (n steps of dt equal one step of n * dt) and 95% of a change takes
    // ln(20) / speed (about 3 / speed) seconds at any frame rate.
    [[nodiscard]] inline float adaptExposure(float previous, float target, float speedUp, float speedDown,
                                             float deltaTime, float minExposure, float maxExposure) noexcept
    {
        // NaN-safe (!(x > 0) is true for NaN): the one persistent chain must never stay stuck on a bad value.
        if (!(previous > 0.0f) || std::isinf(previous))
            previous = 1.0f;
        // VK-1670: speedUp when the scene got brighter (the exposure falls), speedDown when it got darker (it rises).
        const float speed = (target < previous) ? speedUp : speedDown;
        const float adapted = previous + (target - previous) * (1.0f - std::exp(-deltaTime * speed));
        return std::fmin(std::fmax(adapted, minExposure), maxExposure);
    }

    // VK-1664: AutoExposureEffect adapts over the REAL time since its previous step. A step is capped so a hitch (a
    // loading stall, a breakpoint) cannot jump the exposure. The floor is 0 because a steady clock never runs
    // backwards; the old 1 ms floor guarded the camera clock (which stops with gameplay time) and inflated every step
    // above 1000 fps. NaN becomes 0 (std::fmax returns the number).
    inline constexpr float maxAdaptationStepSeconds = 0.5f;
    // The first step after init has no previous step: a nominal 60 Hz frame, as before.
    inline constexpr float firstAdaptationStepSeconds = 0.016f;

    [[nodiscard]] inline float clampAdaptationDelta(float seconds) noexcept
    {
        return std::fmin(std::fmax(seconds, 0.0f), maxAdaptationStepSeconds);
    }
}
