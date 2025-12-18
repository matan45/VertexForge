#type VERTEX
#version 460 core

layout(location = 0) in vec2 inPosition;   // Quad corner offset (-0.5 to 0.5)
layout(location = 1) in vec2 inTexCoord;   // UV coordinates

layout(location = 2) in vec4 inWorldPosAndAtlas;  // xyz = world position, w = atlas index
layout(location = 3) in vec2 inSize;              // Size in pixels (screen) or world units
layout(location = 4) in uint inSizeMode;          // 0 = ScreenSpace, 1 = WorldSpace
layout(location = 5) in uint inEntityId;          // Entity ID for picking
layout(location = 6) in vec4 inColorTint;         // RGBA color tint

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColorTint;
layout(location = 2) out flat uint fragEntityId;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float padding;
} camera;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    float atlasGridSize;
    float padding;
} pc;

void main() {
    vec3 worldPos = inWorldPosAndAtlas.xyz;
    float atlasIndex = inWorldPosAndAtlas.w;

    // Extract camera right and up vectors from view matrix
    vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
    vec3 cameraUp = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);

    vec3 vertexPos;

    if (inSizeMode == 0u) {
        // Screen-space mode: constant size in pixels regardless of distance

        // First, project the billboard center to clip space
        vec4 centerClip = camera.projection * camera.view * vec4(worldPos, 1.0);

        // Calculate the offset in normalized device coordinates
        // inPosition is -0.5 to 0.5, so we scale by size/viewportSize
        vec2 ndcOffset = inPosition * inSize / pc.viewportSize * 2.0;

        // Apply offset in clip space (multiply by w to maintain proper perspective)
        gl_Position = centerClip;
        gl_Position.xy += ndcOffset * centerClip.w;
    }
    else {
        // World-space mode: size scales with distance (like a physical object)

        // Calculate billboard vertex position in world space
        vertexPos = worldPos
            + cameraRight * inPosition.x * inSize.x
            + cameraUp * inPosition.y * inSize.y;

        gl_Position = camera.projection * camera.view * vec4(vertexPos, 1.0);
    }

    float gridSize = pc.atlasGridSize;
    float tileU = mod(atlasIndex, gridSize);
    float tileV = floor(atlasIndex / gridSize);

    // Map input UV (0-1) to tile UV within atlas
    vec2 tileSize = vec2(1.0 / gridSize);
    fragTexCoord = (vec2(tileU, tileV) + inTexCoord) * tileSize;

    // Pass through color tint and entity ID
    fragColorTint = inColorTint;
    fragEntityId = inEntityId;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColorTint;
layout(location = 2) in flat uint fragEntityId;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D atlasTexture;

void main() {
    vec4 texColor = texture(atlasTexture, fragTexCoord);

    // Apply color tint
    vec4 finalColor = texColor * fragColorTint;

    if (finalColor.a < 0.01) {
        discard;
    }

    outColor = finalColor;
}
