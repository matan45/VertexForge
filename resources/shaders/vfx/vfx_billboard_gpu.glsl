#type VERTEX
#version 460 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragLifetimeRatio;

// Must match C++ GPUParticle exactly (VK-238 updated)
struct GPUParticle
{
    vec3 position;
    float lifetime;
    vec3 velocity;
    float maxLifetime;
    vec4 color;
    float size;
    float rotation;         // VK-238: Rotation angle in radians
    float initialSize;      // VK-238: For modifier calculations
    float initialSpeed;     // VK-238: For modifier calculations
};

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
} camera;

layout(std430, set = 0, binding = 2) readonly buffer ParticleBuffer {
    GPUParticle particles[];
};

void main() {
    // Get particle index from instance index
    // firstInstance in indirect draw command contains the particle offset
    uint particleIdx = gl_InstanceIndex;

    GPUParticle p = particles[particleIdx];

    // VK-238: Early exit for inactive particles (size <= 0 means inactive)
    if (p.size <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);  // Behind camera
        fragTexCoord = vec2(0.0);
        fragColor = vec4(0.0);
        fragLifetimeRatio = 1.0;
        return;
    }

    // Extract camera right and up vectors from view matrix for billboarding
    vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
    vec3 cameraUp = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);

    // VK-238: Apply rotation to local quad position
    float cosR = cos(p.rotation);
    float sinR = sin(p.rotation);
    vec2 rotatedPos;
    rotatedPos.x = inPosition.x * cosR - inPosition.y * sinR;
    rotatedPos.y = inPosition.x * sinR + inPosition.y * cosR;

    // Calculate billboard vertex position in world space (using rotated local position)
    vec3 vertexPos = p.position
        + cameraRight * rotatedPos.x * p.size
        + cameraUp * rotatedPos.y * p.size;

    gl_Position = camera.projection * camera.view * vec4(vertexPos, 1.0);

    float lifetimeRatio = (p.maxLifetime > 0.0) ? (p.lifetime / p.maxLifetime) : 0.0;

    fragTexCoord = inTexCoord;
    fragColor = p.color;
    fragLifetimeRatio = lifetimeRatio;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragLifetimeRatio;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D particleTexture;

void main() {
    vec4 texColor = texture(particleTexture, fragTexCoord);

    vec4 finalColor = texColor * fragColor;

    // VK-238: Skip default fade - modifiers handle color now
    // Only apply fragment discard for near-transparent pixels
    if (finalColor.a < 0.01) {
        discard;
    }

    outColor = finalColor;
}
