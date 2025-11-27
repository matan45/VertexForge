#type VERTEX
#version 460 core
layout (location = 0) in vec3 position;

layout(binding = 0) uniform UniformBufferObject
{
    mat4 projection;
    mat4 view;
}ubo;

layout (location = 0) out vec3 localPos;

void main()
{
    localPos = position;

    // Remove translation from view matrix, keep only rotation
    mat4 rotView = mat4(mat3(ubo.view));
    vec4 clipPos = ubo.projection * rotView * vec4(position, 1.0);

    // Set z = w so depth is always at far plane
    gl_Position = clipPos.xyww;
}

#type FRAGMENT
#version 460 core
layout (location = 0) out vec4 FragColor;
layout (location = 0) in vec3 localPos;

layout(binding = 1) uniform samplerCube environmentMap;

// Debug mode: set to 1 to see face colors, 0 for normal rendering
#define DEBUG_MODE 1

void main()
{
    vec3 dir = normalize(localPos);

#if DEBUG_MODE
    // Debug: show which face we're sampling based on direction
    vec3 absDir = abs(dir);
    vec3 debugColor;

    if (absDir.x > absDir.y && absDir.x > absDir.z) {
        // X face
        debugColor = dir.x > 0 ? vec3(1, 0, 0) : vec3(0.5, 0, 0);  // +X = Red, -X = Dark Red
    } else if (absDir.y > absDir.x && absDir.y > absDir.z) {
        // Y face
        debugColor = dir.y > 0 ? vec3(0, 1, 0) : vec3(0, 0.5, 0);  // +Y = Green, -Y = Dark Green
    } else {
        // Z face
        debugColor = dir.z > 0 ? vec3(0, 0, 1) : vec3(0, 0, 0.5);  // +Z = Blue, -Z = Dark Blue
    }

    // Mix debug color with actual texture to see both
    vec3 envColor = texture(environmentMap, dir).rgb;
    envColor = pow(envColor, vec3(1.0/2.2));
    FragColor = vec4(mix(envColor, debugColor, 0.3), 1.0);
#else
    vec3 envColor = texture(environmentMap, dir).rgb;
    envColor = pow(envColor, vec3(1.0/2.2));
    FragColor = vec4(envColor, 1.0);
#endif
}