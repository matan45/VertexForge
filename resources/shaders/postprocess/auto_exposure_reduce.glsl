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

void main()
{
    uint idx = gl_LocalInvocationIndex;
    sharedHistogram[idx] = histogram.bins[idx];
    barrier();

    if (idx == 0)
    {
        float totalPixels = float(pc.pixelCount);
        float lowCount = totalPixels * pc.lowPercentile;
        float highCount = totalPixels * pc.highPercentile;

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

                float logLum = (float(i) / 255.0) * pc.logLuminanceRange + pc.minLogLuminance;
                weightedLogSum += logLum * contribution;
                sum += contribution;
            }
        }

        float avgLogLum = (sum > 0.0) ? (weightedLogSum / sum) : pc.minLogLuminance;
        float avgLum = exp2(avgLogLum);

        float targetExp = 0.18 / max(avgLum, 0.001);
        targetExp *= exp2(pc.exposureCompensation);
        targetExp = clamp(targetExp, pc.minExposure, pc.maxExposure);

        // NaN-safe (!(x > 0) is true for NaN): the one persistent chain must never stay stuck on a bad value.
        float prevExposure = exposure.state.currentExposure;
        if (!(prevExposure > 0.0) || isinf(prevExposure)) prevExposure = 1.0;

        float speed = (targetExp > prevExposure) ? pc.adaptSpeedUp : pc.adaptSpeedDown;
        float adaptedExposure = prevExposure + (targetExp - prevExposure) * (1.0 - exp(-pc.deltaTime * speed));
        adaptedExposure = clamp(adaptedExposure, pc.minExposure, pc.maxExposure);

        ExposureState result = ExposureState(adaptedExposure, targetExp, avgLum, 0.0);
        exposure.state = result;
        exposure.readback[pc.readbackSlot] = result;
    }
}
