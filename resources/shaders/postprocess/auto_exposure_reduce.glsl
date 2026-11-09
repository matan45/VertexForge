#type COMPUTE
#version 460 core

layout(local_size_x = 256) in;

layout(set = 0, binding = 0) buffer Histogram { uint bins[256]; } histogram;

// render::postprocess::AutoExposureState / AutoExposureBuffer (AutoExposureEffect.hpp), field for field.
struct ExposureState
{
    float currentExposure;
    float targetExposure;
    float avgLuminance;
    float padding;
};

// VK-1664: ONE adaptation chain (state), plus the host readback copy for each frame in flight. The CPU reads a copy
// MAX_FRAMES_IN_FLIGHT frames after this shader wrote it.
layout(set = 0, binding = 1) buffer Exposure {
    ExposureState state;
    ExposureState readback[2];
} exposure;

layout(push_constant) uniform PushConstants {
    float minLogLuminance;
    float logLuminanceRange;
    float lowPercentile;
    float highPercentile;
    float adaptSpeedUp;
    float adaptSpeedDown;
    float deltaTime;
    float exposureCompensation;
    float minExposure;
    float maxExposure;
    uint pixelCount;
    uint readbackSlot;
} pc;

shared uint sharedHistogram[256];

// VK-1664: binLogLuminance, meteredExposure, adaptExposure and the percentile loop in main have CPU twins in
// VFEngine/graphics/render/postprocess/ExposureMath.hpp (the loop: percentileAverageLogLuminance). Keep each identical
// to its twin (pinned by test_auto_exposure).

// The log2 luminance at the centre of a bin. auto_exposure_histogram.glsl stores round(t * 254) for the normalised log
// luminance t, so decode with 254 too: dividing by 255 read every bin low (up to 0.086 EV of over-exposure).
float binLogLuminance(uint bin, float minLogLuminance, float logLuminanceRange)
{
    return (float(bin) / 254.0) * logLuminanceRange + minLogLuminance;
}

// The multiplier that brings the average luminance to middle grey (0.18), shifted by the compensation (EV) and clamped.
float meteredExposure(float avgLuminance, float exposureCompensation, float minExposure, float maxExposure)
{
    float target = 0.18 / max(avgLuminance, 0.001) * exp2(exposureCompensation);
    return clamp(target, minExposure, maxExposure);
}

// One step of the ONE adaptation chain over deltaTime seconds of real time (AutoExposureEffect's own clock). It covers
// 1 - exp(-deltaTime * speed) of the remaining distance, so 95% of a change takes ln(20) / speed (about 3 / speed)
// seconds at any frame rate.
float adaptExposure(float previous, float target, float speedUp, float speedDown, float deltaTime,
                    float minExposure, float maxExposure)
{
    // NaN-safe (!(x > 0) is true for NaN): the one persistent chain must never stay stuck on a bad value.
    if (!(previous > 0.0) || isinf(previous)) previous = 1.0;
    // VK-1670: speedUp when the scene got brighter (the exposure falls), speedDown when it got darker (it rises).
    float speed = (target < previous) ? speedUp : speedDown;
    float adapted = previous + (target - previous) * (1.0 - exp(-deltaTime * speed));
    return clamp(adapted, minExposure, maxExposure);
}

void main()
{
    uint idx = gl_LocalInvocationIndex;
    sharedHistogram[idx] = histogram.bins[idx];
    barrier();

    if (idx == 0)
    {
        float totalPixels = float(pc.pixelCount);
        float lowCount = totalPixels * pc.lowPercentile;
        // VK-1664: at least one pixel wide. low == high (the loader allows 0.5 / 0.5) left every contribution 0, so the
        // average fell back to minLogLuminance and the exposure to maxExposure.
        float highCount = max(totalPixels * pc.highPercentile, lowCount + 1.0);

        float sum = 0.0;
        float weightedLogSum = 0.0;
        float runningCount = 0.0;

        for (uint i = 0; i < 256; ++i)
        {
            float binCount = float(sharedHistogram[i]);
            float prevRunning = runningCount;
            runningCount += binCount;

            if (runningCount > lowCount && prevRunning < highCount)
            {
                float contribution = binCount;
                if (prevRunning < lowCount)
                    contribution -= (lowCount - prevRunning);
                if (runningCount > highCount)
                    contribution -= (runningCount - highCount);

                weightedLogSum += binLogLuminance(i, pc.minLogLuminance, pc.logLuminanceRange) * contribution;
                sum += contribution;
            }
        }

        float avgLogLum = (sum > 0.0) ? (weightedLogSum / sum) : pc.minLogLuminance;
        float avgLum = exp2(avgLogLum);
        float targetExp = meteredExposure(avgLum, pc.exposureCompensation, pc.minExposure, pc.maxExposure);
        float adaptedExposure = adaptExposure(exposure.state.currentExposure, targetExp, pc.adaptSpeedUp,
                                              pc.adaptSpeedDown, pc.deltaTime, pc.minExposure, pc.maxExposure);

        ExposureState result = ExposureState(adaptedExposure, targetExp, avgLum, 0.0);
        exposure.state = result;
        exposure.readback[pc.readbackSlot] = result;
    }
}
