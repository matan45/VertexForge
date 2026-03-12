#type COMPUTE
#version 460 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rgba32f, set = 0, binding = 0) writeonly uniform image2D h0Spectrum;

layout(push_constant) uniform PushConstants {
    uint N;
    float patchSize;
    float windSpeed;
    float windDirX;
    float windDirZ;
    float amplitude;
    float gravity;
    float cutoffLow;
    uint seed;
    uint padding;
} pc;

const float PI = 3.14159265358979323846;

// PCG hash for deterministic randomness
uint pcgHash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float hashToFloat(uint h) {
    return float(h) / 4294967296.0;
}

// Box-Muller transform: uniform -> Gaussian
vec2 gaussianRandom(uint s1, uint s2) {
    float u1 = max(hashToFloat(pcgHash(s1)), 1e-6);
    float u2 = hashToFloat(pcgHash(s2));
    float r = sqrt(-2.0 * log(u1));
    float theta = 2.0 * PI * u2;
    return vec2(r * cos(theta), r * sin(theta));
}

float phillipsSpectrum(vec2 k, float kLen, float L, vec2 windDir, float A) {
    if (kLen < 0.0001) return 0.0;

    float kLen2 = kLen * kLen;
    float kLen4 = kLen2 * kLen2;

    float kDotW = dot(normalize(k), windDir);

    float P = A * exp(-1.0 / (kLen2 * L * L)) / kLen4 * kDotW * kDotW;

    float l = pc.cutoffLow;
    P *= exp(-kLen2 * l * l);

    return P;
}

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;

    if (x >= pc.N || y >= pc.N) return;

    int N = int(pc.N);

    // Wave vector in standard FFT order (DC at corner)
    int nx = (x < pc.N / 2u) ? int(x) : int(x) - N;
    int nz = (y < pc.N / 2u) ? int(y) : int(y) - N;

    float kx = 2.0 * PI * float(nx) / pc.patchSize;
    float kz = 2.0 * PI * float(nz) / pc.patchSize;
    vec2 k = vec2(kx, kz);
    float kLen = length(k);

    vec2 windDir = normalize(vec2(pc.windDirX, pc.windDirZ));
    float L = pc.windSpeed * pc.windSpeed / pc.gravity;

    float sqrtPk = sqrt(max(phillipsSpectrum(k, kLen, L, windDir, pc.amplitude), 0.0));
    float sqrtPnk = sqrt(max(phillipsSpectrum(-k, kLen, L, windDir, pc.amplitude), 0.0));

    uint baseSeed = (y * pc.N + x) * 4u + pc.seed;
    vec2 gauss1 = gaussianRandom(baseSeed, baseSeed + 1u);

    uint negX = (pc.N - x) % pc.N;
    uint negY = (pc.N - y) % pc.N;
    uint negSeed = (negY * pc.N + negX) * 4u + pc.seed;
    vec2 gauss2 = gaussianRandom(negSeed, negSeed + 1u);

    // h0(k) = (1/sqrt(2)) * gauss * sqrt(P(k))
    float invSqrt2 = 0.70710678118;
    vec2 h0k = invSqrt2 * gauss1 * sqrtPk;

    // h0conj(-k) = conjugate of h0(-k) = (1/sqrt(2)) * conj(gauss2) * sqrt(P(-k))
    vec2 h0nk = invSqrt2 * vec2(gauss2.x, -gauss2.y) * sqrtPnk;

    // Store: RG = h0(k), BA = h0conj(-k)
    imageStore(h0Spectrum, ivec2(x, y), vec4(h0k, h0nk));
}
