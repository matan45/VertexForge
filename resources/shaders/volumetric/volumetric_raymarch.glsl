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
    vec4 ambientParams;
    vec4 cameraPosition;
};

layout(rgba16f, set = 0, binding = 3) uniform readonly image3D temporalOutput;
layout(rgba16f, set = 0, binding = 4) uniform writeonly image3D integratedVolume;

float sliceToDepth(float slice, float near, float far, float numSlices) {
    float t = slice / numSlices;
    return near * pow(far / near, t);
}

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    uvec3 dims = gridDimensions.xyz;

    if (pixelCoord.x >= int(dims.x) || pixelCoord.y >= int(dims.y))
        return;

    float near = depthParams.x;
    float far = depthParams.y;

    vec3 accumulatedScattering = vec3(0.0);
    float accumulatedTransmittance = 1.0;

    for (uint z = 0; z < dims.z; ++z) {
        ivec3 coord = ivec3(pixelCoord, z);

        vec4 scatteringData = imageLoad(temporalOutput, coord);
        vec3 inScattered = scatteringData.rgb;
        float extinction = scatteringData.a;

        float depthFront = sliceToDepth(float(z), near, far, float(dims.z));
        float depthBack = sliceToDepth(float(z + 1), near, far, float(dims.z));
        float sliceThickness = depthBack - depthFront;

        float sliceTransmittance = exp(-extinction * sliceThickness);

        // Analytical integration of in-scattering within the slice
        // Using the formula: integral(L * exp(-sigma_t * t)) dt from 0 to thickness
        // = L * (1 - exp(-sigma_t * thickness)) / sigma_t
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
