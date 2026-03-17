#type COMPUTE
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Set 0: Volumetric Grid (VolumetricGridManager)
layout(std140, set = 0, binding = 0) uniform VolumetricParamsUBO {
    uvec4 gridDimensions;       // xyz = width, height, depth
    vec4 depthParams;            // x = near, y = far, z = log(far/near), w = 1/log(far/near)
    mat4 invViewProjection;
    mat4 prevViewProjection;
    vec4 fogParams;
    vec4 scatterParams;
    vec4 fogColor;
    vec4 ambientParams;          // z = frameIndex
    vec4 cameraPosition;
};

layout(rgba16f, set = 0, binding = 3) uniform readonly image3D temporalOutput;
layout(rgba16f, set = 0, binding = 4) uniform writeonly image3D integratedVolume;

float sliceToDepth(float slice, float near, float far, float numSlices) {
    float t = slice / numSlices;
    return near * pow(far / near, t);
}

// Interleaved gradient noise - low discrepancy, no visible patterns
float interleavedGradientNoise(vec2 screenPos, float frameIndex) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    float x = magic.x * (screenPos.x + frameIndex * 59.0) + magic.y * screenPos.y;
    return fract(magic.z * fract(x));
}

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    uvec3 dims = gridDimensions.xyz;

    if (pixelCoord.x >= int(dims.x) || pixelCoord.y >= int(dims.y))
        return;

    float near = depthParams.x;
    float far = depthParams.y;

    // Per-pixel temporal jitter to break banding
    float frameIdx = ambientParams.z;
    float jitter = interleavedGradientNoise(vec2(pixelCoord), frameIdx);

    vec3 accumulatedScattering = vec3(0.0);
    float accumulatedTransmittance = 1.0;

    for (uint z = 0; z < dims.z; ++z) {
        ivec3 coord = ivec3(pixelCoord, z);

        vec4 scatteringData = imageLoad(temporalOutput, coord);
        vec3 inScattered = scatteringData.rgb;
        float extinction = scatteringData.a;

        // Jittered depth boundaries to smooth out slice transitions
        float depthFront = sliceToDepth(float(z) + jitter, near, far, float(dims.z));
        float depthBack = sliceToDepth(float(z + 1) + jitter, near, far, float(dims.z));
        float sliceThickness = depthBack - depthFront;

        float sliceTransmittance = exp(-extinction * sliceThickness);

        // Analytical integration of in-scattering within the slice
        vec3 sliceScattering;
        if (extinction > 0.0001) {
            sliceScattering = inScattered * (1.0 - sliceTransmittance) / extinction;
        } else {
            sliceScattering = inScattered * sliceThickness;
        }

        accumulatedScattering += accumulatedTransmittance * sliceScattering;
        accumulatedTransmittance *= sliceTransmittance;

        imageStore(integratedVolume, coord, vec4(accumulatedScattering, accumulatedTransmittance));
    }
}
