#type COMPUTE
#version 460 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rgba32f, set = 0, binding = 0) readonly uniform image2D h0Spectrum;
layout(rg32f, set = 0, binding = 1) writeonly uniform image2D hktDy;
layout(rg32f, set = 0, binding = 2) writeonly uniform image2D hktDx;
layout(rg32f, set = 0, binding = 3) writeonly uniform image2D hktDz;

layout(push_constant) uniform PushConstants {
    uint N;
    float time;
    float choppiness;
    float patchSize;
    float gravity;
    uint padding1;
    uint padding2;
    uint padding3;
} pc;

const float PI = 3.14159265358979323846;

vec2 complexMul(vec2 a, vec2 b) {
    return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;

    if (x >= pc.N || y >= pc.N) return;

    int N = int(pc.N);

    // Wave vector
    int nx = (x < pc.N / 2u) ? int(x) : int(x) - N;
    int nz = (y < pc.N / 2u) ? int(y) : int(y) - N;

    float kx = 2.0 * PI * float(nx) / pc.patchSize;
    float kz = 2.0 * PI * float(nz) / pc.patchSize;
    float kLen = length(vec2(kx, kz));

    // Deep water dispersion relation
    float omega = sqrt(pc.gravity * max(kLen, 0.0001));

    vec4 h0 = imageLoad(h0Spectrum, ivec2(x, y));
    vec2 h0k = h0.rg;
    vec2 h0conjNegK = h0.ba;

    // Time evolution: h(k,t) = h0(k)*exp(iwt) + h0conj(-k)*exp(-iwt)
    float phase = omega * pc.time;
    vec2 expPos = vec2(cos(phase), sin(phase));
    vec2 expNeg = vec2(cos(phase), -sin(phase));

    vec2 hkt = complexMul(h0k, expPos) + complexMul(h0conjNegK, expNeg);

    imageStore(hktDy, ivec2(x, y), vec4(hkt, 0.0, 0.0));

    // Horizontal displacements (Dx, Dz) = -i * (kx,kz)/|k| * h(k,t) * choppiness
    if (kLen > 0.0001) {
        // -i * complex = vec2(imag, -real)
        vec2 minusI_hkt = vec2(hkt.y, -hkt.x);
        float invK = pc.choppiness / kLen;

        vec2 dx = minusI_hkt * kx * invK;
        vec2 dz = minusI_hkt * kz * invK;

        imageStore(hktDx, ivec2(x, y), vec4(dx, 0.0, 0.0));
        imageStore(hktDz, ivec2(x, y), vec4(dz, 0.0, 0.0));
    } else {
        imageStore(hktDx, ivec2(x, y), vec4(0.0));
        imageStore(hktDz, ivec2(x, y), vec4(0.0));
    }
}
