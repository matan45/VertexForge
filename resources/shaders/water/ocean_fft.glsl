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

    uint coord = (pc.direction == 0u) ? x : y;

    uint halfButterfly = 1u << pc.stage;
    uint butterflySize = halfButterfly << 1u;

    uint localIdx = coord % butterflySize;
    uint pairIdx = localIdx % halfButterfly;

    uint groupStart = (coord / butterflySize) * butterflySize;
    uint topCoord = groupStart + pairIdx;
    uint botCoord = topCoord + halfButterfly;

    vec2 topVal, botVal;
    if (pc.direction == 0u) {
        topVal = imageLoad(inputImg, ivec2(topCoord, y)).rg;
        botVal = imageLoad(inputImg, ivec2(botCoord, y)).rg;
    } else {
        topVal = imageLoad(inputImg, ivec2(x, topCoord)).rg;
        botVal = imageLoad(inputImg, ivec2(x, botCoord)).rg;
    }

    // Twiddle factor for IFFT: exp(+2*pi*i * k / M)
    float angle = 2.0 * PI * float(pairIdx) / float(butterflySize);
    vec2 twiddle = vec2(cos(angle), sin(angle));
    vec2 twiddled = complexMul(botVal, twiddle);

    vec2 result;
    if (localIdx < halfButterfly) {
        result = topVal + twiddled;
    } else {
        result = topVal - twiddled;
    }

    imageStore(outputImg, ivec2(x, y), vec4(result, 0.0, 0.0));
}
