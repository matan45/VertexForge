#type COMPUTE
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Reactive/transparency mask for temporal upscalers (DLSS transparency hint).
// Marks pixels whose color was changed by transparency (VFX particles, WBOIT,
// distortion) by diffing the final scene color against an opaque-only copy
// captured before the transparency passes — the same approach as FSR's
// auto-reactive helper.

layout(set = 0, binding = 0) uniform sampler2D opaqueColor;   // pre-transparency scene color
layout(set = 0, binding = 1) uniform sampler2D finalColor;    // post-transparency scene color
layout(set = 0, binding = 2, r8) uniform writeonly image2D reactiveMask;

// How aggressively a color delta saturates the mask
const float REACTIVE_SCALE = 4.0;
// Upscaler-recommended ceiling: a fully-reactive pixel discards all history
const float REACTIVE_MAX = 0.9;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 resolution = imageSize(reactiveMask);

    if (pixel.x >= resolution.x || pixel.y >= resolution.y)
        return;

    vec2 uv = (vec2(pixel) + 0.5) / vec2(resolution);

    vec3 opaque = texture(opaqueColor, uv).rgb;
    vec3 final = texture(finalColor, uv).rgb;

    vec3 diff = abs(final - opaque);
    float delta = max(diff.r, max(diff.g, diff.b));

    // Normalize against scene brightness so dim smoke over dark ground still registers
    float reference = max(max(opaque.r, max(opaque.g, opaque.b)), 0.05);
    float mask = clamp(delta / reference * REACTIVE_SCALE, 0.0, REACTIVE_MAX);

    imageStore(reactiveMask, pixel, vec4(mask, 0.0, 0.0, 0.0));
}
