#type COMPUTE
#version 460 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rg32f,   set = 0, binding = 0) readonly  uniform image2D heightField;
layout(rg32f,   set = 0, binding = 1) readonly  uniform image2D chopXField;
layout(rg32f,   set = 0, binding = 2) readonly  uniform image2D chopZField;
layout(rgba16f, set = 0, binding = 3) writeonly  uniform image2D displacementMap;
layout(rgba16f, set = 0, binding = 4) writeonly  uniform image2D normalMap;

layout(push_constant) uniform PushConstants {
    uint N;
    float choppiness;
    float patchSize;
    float foamThreshold;
} pc;

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;

    if (x >= pc.N || y >= pc.N) return;

    // Read IFFT results (real part only — .r component)
    // Apply sign correction (-1)^(x+y) for standard FFT ordering
    float sign = ((x + y) % 2u == 0u) ? 1.0 : -1.0;

    // No /N^2 normalization: Phillips spectrum is designed for unnormalized DFT (Tessendorf)
    float dy = imageLoad(heightField, ivec2(x, y)).r * sign;
    float dx = imageLoad(chopXField, ivec2(x, y)).r * sign;
    float dz = imageLoad(chopZField, ivec2(x, y)).r * sign;

    // Jacobian determinant for foam detection
    // Approximate partial derivatives via finite differences on displacement
    int xp = int((x + 1u) % pc.N);
    int xm = int((x + pc.N - 1u) % pc.N);
    int yp = int((y + 1u) % pc.N);
    int ym = int((y + pc.N - 1u) % pc.N);

    float signXP = (((x + 1u) + y) % 2u == 0u) ? 1.0 : -1.0;
    float signXM = (((x + pc.N - 1u) + y) % 2u == 0u) ? 1.0 : -1.0;
    float signYP = ((x + (y + 1u)) % 2u == 0u) ? 1.0 : -1.0;
    float signYM = ((x + (y + pc.N - 1u)) % 2u == 0u) ? 1.0 : -1.0;

    float dxdx = imageLoad(chopXField, ivec2(xp, y)).r * signXP -
                 imageLoad(chopXField, ivec2(xm, y)).r * signXM;
    float dzdz = imageLoad(chopZField, ivec2(x, yp)).r * signYP -
                 imageLoad(chopZField, ivec2(x, ym)).r * signYM;
    float dxdz = imageLoad(chopXField, ivec2(x, yp)).r * signYP -
                 imageLoad(chopXField, ivec2(x, ym)).r * signYM;

    // Jacobian = (1 + dDx/dx)(1 + dDz/dz) - (dDx/dz)^2
    // Choppiness is already baked into chopX/chopZ by time_evolve shader
    float texelSize = pc.patchSize / float(pc.N);
    float derivScale = 1.0 / (2.0 * texelSize);
    float Jxx = 1.0 + dxdx * derivScale;
    float Jzz = 1.0 + dzdz * derivScale;
    float Jxz = dxdz * derivScale;
    float jacobian = Jxx * Jzz - Jxz * Jxz;

    // Foam where Jacobian < threshold (wave folding), wide smooth falloff
    float foam = smoothstep(pc.foamThreshold + 0.5, pc.foamThreshold - 0.2, jacobian);

    // Store displacement (Dx, Dy, Dz, foam)
    imageStore(displacementMap, ivec2(x, y), vec4(dx, dy, dz, foam));

    // Compute normal from height gradients
    float dyDx = imageLoad(heightField, ivec2(xp, y)).r * signXP -
                 imageLoad(heightField, ivec2(xm, y)).r * signXM;
    float dyDz = imageLoad(heightField, ivec2(x, yp)).r * signYP -
                 imageLoad(heightField, ivec2(x, ym)).r * signYM;

    // Central difference derivative: dh/dx ≈ (h(x+1) - h(x-1)) / (2 * texelSize)
    float normalDerivScale = 1.0 / (2.0 * texelSize);
    vec3 normal = normalize(vec3(-dyDx * normalDerivScale, 1.0, -dyDz * normalDerivScale));

    imageStore(normalMap, ivec2(x, y), vec4(normal, foam));
}
