#type COMPUTE
#version 450

// Volumetric Temporal Reprojection Compute Shader
// Blends current frame scattering with previous frame history
// to reduce noise and improve quality over time

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
    vec4 ambientParams;          // y = temporalBlendFactor
    vec4 cameraPosition;
};

// Binding 1: current scattering (read as storage image)
layout(rgba16f, set = 0, binding = 1) uniform readonly image3D scatteringVolume;

// Binding 2: previous frame history (read as sampled image)
layout(set = 0, binding = 2) uniform sampler3D historyRead;

// Binding 3: current frame history output (write as storage image)
layout(rgba16f, set = 0, binding = 3) uniform writeonly image3D historyWrite;

// Logarithmic depth slice to linear depth
float sliceToDepth(float slice, float near, float far, float numSlices) {
    float t = slice / numSlices;
    return near * pow(far / near, t);
}

// Linear depth to logarithmic depth slice
float depthToSlice(float depth, float near, float far, float numSlices) {
    float logRatio = log(depth / near) / log(far / near);
    return clamp(logRatio * numSlices, 0.0, numSlices - 1.0);
}

void main() {
    ivec3 froxelCoord = ivec3(gl_GlobalInvocationID.xyz);
    uvec3 dims = gridDimensions.xyz;

    if (froxelCoord.x >= int(dims.x) || froxelCoord.y >= int(dims.y) || froxelCoord.z >= int(dims.z))
        return;

    // Read current frame scattering
    vec4 currentScattering = imageLoad(scatteringVolume, froxelCoord);

    // Reconstruct world-space position from froxel coordinates
    vec2 uv = (vec2(froxelCoord.xy) + 0.5) / vec2(dims.xy);
    float near = depthParams.x;
    float far = depthParams.y;
    float depth = sliceToDepth(float(froxelCoord.z) + 0.5, near, far, float(dims.z));

    vec2 ndc = uv * 2.0 - 1.0;
    float ndcDepth = (far * (depth - near)) / (depth * (far - near));

    vec4 clipPos = vec4(ndc, ndcDepth, 1.0);
    vec4 worldPos = invViewProjection * clipPos;
    worldPos.xyz /= worldPos.w;

    // Project world position through previous frame's view-projection
    vec4 prevClip = prevViewProjection * vec4(worldPos.xyz, 1.0);
    vec3 prevNDC = prevClip.xyz / prevClip.w;

    // Convert to previous frame froxel UV
    vec2 prevUV = prevNDC.xy * 0.5 + 0.5;

    // Compute previous frame depth and slice
    float prevDepth = depth; // Approximate - world position is the same, depth similar
    float prevSlice = depthToSlice(prevDepth, near, far, float(dims.z));
    float prevW = (prevSlice + 0.5) / float(dims.z);

    vec3 historyUVW = vec3(prevUV, prevW);

    float blendFactor = ambientParams.y; // temporal blend factor

    // Check if previous UV is within valid range (not disoccluded)
    if (all(greaterThanEqual(historyUVW.xy, vec2(0.0))) &&
        all(lessThanEqual(historyUVW.xy, vec2(1.0))) &&
        historyUVW.z >= 0.0 && historyUVW.z <= 1.0) {
        // Sample history with trilinear filtering
        vec4 historyValue = texture(historyRead, historyUVW);

        // Blend: high blend factor = more history (smoother but more ghosting)
        vec4 result = mix(currentScattering, historyValue, blendFactor);
        imageStore(historyWrite, froxelCoord, result);
    } else {
        // Disoccluded - use current frame only
        imageStore(historyWrite, froxelCoord, currentScattering);
    }
}
