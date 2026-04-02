#type COMPUTE
#version 460 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rg32f, set = 0, binding = 0) readonly uniform image2D inputImg;
layout(rg32f, set = 0, binding = 1) writeonly uniform image2D outputImg;

layout(push_constant) uniform PushConstants {
    uint N;
    uint stage;       // Current butterfly stage (0..log2(N)-1)
    uint direction;   // 0 = horizontal, 1 = vertical
    uint padding;
} pc;

const float PI = 3.14159265358979323846;

vec2 complexMul(vec2 a, vec2 b) {
    return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;

    if (x >= pc.N || y >= pc.N) return;

    uint j = (pc.direction == 0u) ? x : y;
    uint halfN = pc.N >> 1u;

    uint stride = 1u << pc.stage;
    uint doubleStride = stride << 1u;

    // Stockham auto-sort: within each doubleStride group,
    // first 'stride' outputs get sum, next 'stride' get difference
    uint group = j / doubleStride;
    uint jInGroup = j % doubleStride;
    bool isTop = (jInGroup < stride);
    uint t = isTop ? jInGroup : (jInGroup - stride);

    // Input halves are always N/2 apart
    uint i0 = group * stride + t;
    uint i1 = i0 + halfN;

    vec2 val0, val1;
    if (pc.direction == 0u) {
        val0 = imageLoad(inputImg, ivec2(i0, y)).rg;
        val1 = imageLoad(inputImg, ivec2(i1, y)).rg;
    } else {
        val0 = imageLoad(inputImg, ivec2(x, i0)).rg;
        val1 = imageLoad(inputImg, ivec2(x, i1)).rg;
    }

    // Twiddle factor for IFFT: exp(+2*pi*i * t / doubleStride)
    float angle = 2.0 * PI * float(t) / float(doubleStride);
    vec2 twiddle = vec2(cos(angle), sin(angle));
    vec2 twiddled = complexMul(val1, twiddle);

    vec2 result;
    if (isTop) {
        result = val0 + twiddled;
    } else {
        result = val0 - twiddled;
    }

    imageStore(outputImg, ivec2(x, y), vec4(result, 0.0, 0.0));
}
