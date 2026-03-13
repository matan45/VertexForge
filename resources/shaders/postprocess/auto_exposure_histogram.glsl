#type COMPUTE
#version 460 core

layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 0, binding = 0) uniform sampler2D hdrScene;
layout(set = 0, binding = 1) buffer Histogram { uint bins[256]; } histogram;

layout(push_constant) uniform PushConstants {
    float minLogLuminance;
    float logLuminanceRange;
    uint width;
    uint height;
} pc;

shared uint sharedHistogram[256];

void main()
{
    uint localIndex = gl_LocalInvocationIndex;

    if (localIndex < 256)
        sharedHistogram[localIndex] = 0;
    barrier();

    uvec2 pixelCoord = gl_GlobalInvocationID.xy;
    if (pixelCoord.x < pc.width && pixelCoord.y < pc.height)
    {
        vec2 uv = (vec2(pixelCoord) + 0.5) / vec2(pc.width, pc.height);
        vec3 color = texture(hdrScene, uv).rgb;

        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));

        if (luminance > 0.001)
        {
            float logLum = clamp((log2(luminance) - pc.minLogLuminance) / pc.logLuminanceRange, 0.0, 1.0);
            uint bin = uint(logLum * 254.0 + 0.5);
            bin = clamp(bin, 1u, 255u);
            atomicAdd(sharedHistogram[bin], 1);
        }
        else
        {
            atomicAdd(sharedHistogram[0], 1);
        }
    }

    barrier();

    if (localIndex < 256)
        atomicAdd(histogram.bins[localIndex], sharedHistogram[localIndex]);
}
