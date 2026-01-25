#type COMPUTE
#version 450

// Light Occlusion Culling Compute Shader
// Tests light bounding spheres against the Hi-Z pyramid and outputs visibility flags

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Light types
const uint LIGHT_TYPE_POINT = 0u;
const uint LIGHT_TYPE_SPOT  = 1u;

layout(set = 0, binding = 0) uniform sampler2D hiZPyramid;

// Light bounding data
struct LightBounds {
    vec4 positionRadius;  // xyz = world position, w = bounding radius
    vec4 direction;       // xyz = direction (spot only), w = cone angle (spot only)
    uint entityId;
    uint lightType;       // 0 = point, 1 = spot
    uint padding0;
    uint padding1;
};

layout(std430, set = 0, binding = 1) readonly buffer LightBoundsBuffer {
    LightBounds lights[];
};

// Output: Visibility flags (1 = visible, 0 = occluded)
layout(std430, set = 0, binding = 2) writeonly buffer VisibilityBuffer {
    uint visibility[];
};

layout(set = 0, binding = 3) uniform CameraUBO {
    mat4 viewProj;
    vec4 screenSize;   // xy = width/height, zw = 1/width, 1/height
    vec4 cameraPos;    // xyz = camera position, w = near plane
    uint lightCount;
    uint hiZMipLevels;
    uint padding0;
    uint padding1;
};

// Project a point to clip space
vec4 projectPoint(vec3 worldPos) {
    return viewProj * vec4(worldPos, 1.0);
}

// Test if a sphere is visible against the Hi-Z buffer
bool testSphereVisible(vec3 center, float radius) {
    // Project sphere center to clip space
    vec4 centerClip = projectPoint(center);

    // Check if sphere is behind near plane
    // Account for sphere radius - if center.w + radius < nearPlane, entire sphere is behind
    float nearPlane = cameraPos.w;
    if (centerClip.w + radius < nearPlane) {
        return false;  // Entirely behind camera
    }

    // If center is behind but sphere extends in front, conservatively visible
    if (centerClip.w < nearPlane) {
        return true;  // Sphere straddles near plane
    }

    // Perspective divide for center
    vec3 centerNDC = centerClip.xyz / centerClip.w;

    // Project radius to screen space
    // The sphere's screen-space radius depends on distance
    // Approximate: screenRadius = worldRadius / depth * focalLength
    // Using simplified approach: project a point at (center + right * radius)
    vec3 camRight = normalize(cross(vec3(0, 1, 0), normalize(cameraPos.xyz - center)));
    vec4 edgeClip = projectPoint(center + camRight * radius);
    vec3 edgeNDC = edgeClip.xyz / edgeClip.w;
    float screenRadius = length(edgeNDC.xy - centerNDC.xy);

    // Convert NDC to UV [0, 1]
    vec2 centerUV = centerNDC.xy * 0.5 + 0.5;

    // Check if entirely off-screen (accounting for radius)
    float uvRadius = screenRadius * 0.5;  // NDC to UV scale
    if (centerUV.x + uvRadius < 0.0 || centerUV.x - uvRadius > 1.0 ||
        centerUV.y + uvRadius < 0.0 || centerUV.y - uvRadius > 1.0) {
        return false;  // Entirely off-screen
    }

    // If partially off-screen, conservatively mark as visible
    if (centerUV.x - uvRadius < 0.0 || centerUV.x + uvRadius > 1.0 ||
        centerUV.y - uvRadius < 0.0 || centerUV.y + uvRadius > 1.0) {
        return true;  // Partially off-screen, conservatively visible
    }

    // Calculate screen coverage to determine mip level
    vec2 screenSize2D = screenSize.xy;
    float screenDiameter = screenRadius * max(screenSize2D.x, screenSize2D.y);

    // Very small lights (< 1 pixel) - conservatively visible
    if (screenDiameter < 1.0) {
        return true;
    }

    // Select mip level based on coverage
    float mipLevel = floor(log2(screenDiameter)) + 1.0;
    mipLevel = clamp(mipLevel, 0.0, float(hiZMipLevels - 1));

    // Sample Hi-Z at center and edges of the sphere's screen projection
    float hiZCenter = textureLod(hiZPyramid, centerUV, mipLevel).r;

    // Sample at 4 points around the sphere for better coverage
    vec2 offset = vec2(uvRadius * 0.7, 0.0);  // 0.7 ≈ sqrt(2)/2 for diagonal
    float hiZ0 = textureLod(hiZPyramid, centerUV + offset, mipLevel).r;
    float hiZ1 = textureLod(hiZPyramid, centerUV - offset, mipLevel).r;
    float hiZ2 = textureLod(hiZPyramid, centerUV + offset.yx, mipLevel).r;
    float hiZ3 = textureLod(hiZPyramid, centerUV - offset.yx, mipLevel).r;

    // Take maximum (furthest) depth from Hi-Z samples
    float maxHiZDepth = max(max(max(hiZ0, hiZ1), max(hiZ2, hiZ3)), hiZCenter);

    // Calculate the nearest depth of the sphere
    // The nearest point of the sphere to the camera in NDC depth
    float sphereNearZ = centerNDC.z - (radius / centerClip.w);

    // Sphere is visible if its nearest point is in front of (or at) the Hi-Z depth
    // In Vulkan: 0 = near, 1 = far, so smaller = closer
    return sphereNearZ <= maxHiZDepth;
}

void main() {
    uint lightIndex = gl_GlobalInvocationID.x;

    if (lightIndex >= lightCount) {
        return;
    }

    LightBounds light = lights[lightIndex];

    vec3 position = light.positionRadius.xyz;
    float radius = light.positionRadius.w;

    // Test sphere visibility against Hi-Z
    bool isVisible = testSphereVisible(position, radius);

    visibility[lightIndex] = isVisible ? 1u : 0u;
}
