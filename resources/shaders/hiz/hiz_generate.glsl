#type COMPUTE
#version 450

// Hi-Z Generation Compute Shader
// Downsamples depth buffer taking MAX depth (conservative occlusion culling)
// Each mip level stores the maximum (furthest) depth in a 2x2 region

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D inputDepth;
layout(set = 0, binding = 1, r32f) uniform writeonly image2D outputMip;

layout(push_constant) uniform PushConstants {
    ivec2 outputSize;
    ivec2 inputSize;
    int isFirstMip;  // 1 = direct copy from depth, 0 = downsample from previous mip
    int padding;
} pc;

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    if (pos.x >= pc.outputSize.x || pos.y >= pc.outputSize.y) {
        return;
    }

    vec2 texelSize = 1.0 / vec2(pc.inputSize);
    float maxDepth;

    if (pc.isFirstMip == 1) {
        // First mip: direct copy from depth buffer (1:1 mapping)
        vec2 uv = (vec2(pos) + 0.5) * texelSize;
        maxDepth = texture(inputDepth, uv).r;
    } else {
        // Subsequent mips: downsample 2x2 from previous mip
        vec2 uv = (vec2(pos) * 2.0 + 1.0) * texelSize;

        // Gather 4 depth samples
        float d00 = texture(inputDepth, uv + vec2(-0.5, -0.5) * texelSize).r;
        float d10 = texture(inputDepth, uv + vec2( 0.5, -0.5) * texelSize).r;
        float d01 = texture(inputDepth, uv + vec2(-0.5,  0.5) * texelSize).r;
        float d11 = texture(inputDepth, uv + vec2( 0.5,  0.5) * texelSize).r;

        // Take maximum depth (furthest) for conservative culling
        maxDepth = max(max(d00, d10), max(d01, d11));
    }

    imageStore(outputMip, pos, vec4(maxDepth, 0.0, 0.0, 0.0));
}
