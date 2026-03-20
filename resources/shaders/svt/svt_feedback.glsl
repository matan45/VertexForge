#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

#include "../common/svt_types.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D depthBuffer;

layout(std430, set = 0, binding = 1) buffer FeedbackBuffer {
    uint feedback[];
};

layout(set = 0, binding = 2) uniform FeedbackParams {
    mat4 invViewProjection;
    vec4 screenParams;       // width, height, 1/width, 1/height
    vec4 svtScaleOffset;     // xy = scale, zw = offset
    uvec4 svtInfo;           // x = virtualSizeLog2, y = tileSizeLog2, z = unused, w = mipLevels
    vec4 cameraPos;          // xyz = camera world position
};

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenSize = ivec2(screenParams.xy);

    if (pixelCoord.x >= screenSize.x || pixelCoord.y >= screenSize.y)
        return;

    // Sample depth
    vec2 uv = (vec2(pixelCoord) + 0.5) * screenParams.zw;
    float depth = texture(depthBuffer, uv).r;

    // Skip sky pixels (reversed-Z: depth >= 1.0 is far plane)
    if (depth >= 1.0 || depth <= 0.0)
        return;

    // Reconstruct world position from depth
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, depth, 1.0);
    vec4 worldPos4 = invViewProjection * clipPos;
    vec3 worldPos = worldPos4.xyz / worldPos4.w;

    // Compute virtual UV from world position (terrain: worldPos.xz)
    vec2 virtualUV = worldPos.xz * svtScaleOffset.xy + svtScaleOffset.zw;

    // Skip if outside virtual texture bounds
    if (any(lessThan(virtualUV, vec2(0.0))) || any(greaterThanEqual(virtualUV, vec2(1.0))))
        return;

    uint virtualSizeLog2 = svtInfo.x;
    uint tileSizeLog2 = svtInfo.y;
    uint mipLevels = svtInfo.w;

    // Compute desired mip level from distance to camera
    float dist = distance(worldPos, cameraPos.xyz);
    // Approximate: 1 texel per screen pixel at distance 1.0
    // Higher distance = coarser mip needed
    float virtualSize = float(1u << virtualSizeLog2);
    float texelsPerWorldUnit = virtualSize * svtScaleOffset.x;
    float screenPixelsPerTexel = texelsPerWorldUnit * screenParams.y / (2.0 * dist * tan(radians(45.0)));
    float desiredMip = max(0.0, -log2(screenPixelsPerTexel));
    uint mipLevel = clamp(uint(desiredMip), 0u, mipLevels - 1u);

    // Compute tile coordinates at this mip level
    uint tilesPerSide = svtTilesPerSide(mipLevel, virtualSizeLog2, tileSizeLog2);
    uvec2 tileCoord = uvec2(virtualUV * float(tilesPerSide));
    tileCoord = clamp(tileCoord, uvec2(0), uvec2(tilesPerSide - 1u));

    // Compute page table index and atomically mark as requested
    uint pageIdx = svtPageTableIndex(tileCoord, mipLevel, virtualSizeLog2, tileSizeLog2);
    atomicOr(feedback[pageIdx], SVT_FEEDBACK_REQUEST | (mipLevel & 0xFu));
}
