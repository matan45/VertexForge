#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D depthBuffer;

struct GPUVSMLight {
    mat4 viewProjection;
    vec4 biasParams;
    vec4 rangeParams;
    vec4 pcssParams;
    ivec4 pageTableInfo; // x=pagesX, y=pagesY, z=pageTableOffset, w=lightType
};

layout(std430, set = 0, binding = 1) readonly buffer VSMLightBuffer {
    GPUVSMLight lights[];
};

layout(std430, set = 0, binding = 2) buffer FeedbackBuffer {
    uint feedback[];
};

layout(set = 0, binding = 3) uniform FeedbackParams {
    mat4 invViewProjection;
    vec4 screenParams; // width, height, 1/width, 1/height
    uint lightCount;
    uint pad0;
    uint pad1;
    uint pad2;
};

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenSize = ivec2(screenParams.xy);

    if (pixelCoord.x >= screenSize.x || pixelCoord.y >= screenSize.y)
        return;

    // Sample depth
    vec2 uv = (vec2(pixelCoord) + 0.5) * screenParams.zw;
    float depth = texture(depthBuffer, uv).r;

    // Skip sky pixels (depth = 1.0 in reversed-Z or 0.0 in normal)
    if (depth >= 1.0 || depth <= 0.0)
        return;

    // Reconstruct world position from depth
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, depth, 1.0);
    vec4 worldPos4 = invViewProjection * clipPos;
    vec3 worldPos = worldPos4.xyz / worldPos4.w;

    // For each VSM light, determine which page this pixel falls in
    for (uint i = 0; i < lightCount; ++i) {
        GPUVSMLight light = lights[i];

        // Skip point lights (type 2) - they use cubemaps, not VSM pages
        if (light.pageTableInfo.w == 2)
            continue;

        // Skip lights with no pages
        if (light.pageTableInfo.x <= 0 || light.pageTableInfo.y <= 0)
            continue;

        // Project world position into light space
        vec4 lsPos = light.viewProjection * vec4(worldPos, 1.0);

        if (lsPos.w <= 0.0)
            continue;

        vec3 ndcLight = lsPos.xyz / lsPos.w;

        // Check if within light's NDC range
        if (any(greaterThan(abs(ndcLight.xy), vec2(1.0))))
            continue;

        // Compute page coordinates
        vec2 lightUV = ndcLight.xy * 0.5 + 0.5;
        ivec2 pageCoord = ivec2(lightUV * vec2(light.pageTableInfo.xy));
        pageCoord = clamp(pageCoord, ivec2(0), light.pageTableInfo.xy - 1);

        // Mark this page as needed
        uint feedbackIdx = uint(light.pageTableInfo.z) + uint(pageCoord.y * light.pageTableInfo.x + pageCoord.x);
        atomicOr(feedback[feedbackIdx], 1u);
    }
}
