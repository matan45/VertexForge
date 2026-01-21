#type VERTEX
#version 460 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragLifetimeRatio;

// Must match C++ GPUParticle exactly
struct GPUParticle
{
    vec3 position;
    float lifetime;
    vec3 velocity;
    float maxLifetime;
    vec4 color;
    float size;
    uint flags;             // Bit 0 = active
    vec2 padding;
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

const uint FLAG_ACTIVE = 1u;

void main() {
    // Get particle index from instance index
    // firstInstance in indirect draw command contains the particle offset
    uint particleIdx = gl_InstanceIndex;

    GPUParticle p = particles[particleIdx];

    // Early exit for inactive particles (move to degenerate position)
    if ((p.flags & FLAG_ACTIVE) == 0u) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);  // Behind camera
        fragTexCoord = vec2(0.0);
        fragColor = vec4(0.0);
        fragLifetimeRatio = 1.0;
        return;
    }

    // Extract camera right and up vectors from view matrix for billboarding
    vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
    vec3 cameraUp = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);

    // Calculate billboard vertex position in world space
    vec3 vertexPos = p.position
        + cameraRight * inPosition.x * p.size
        + cameraUp * inPosition.y * p.size;

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

    float fadeStart = 0.8;
    if (fragLifetimeRatio > fadeStart) {
        float fadeProgress = (fragLifetimeRatio - fadeStart) / (1.0 - fadeStart);
        finalColor.a *= 1.0 - smoothstep(0.0, 1.0, fadeProgress);
    }

    if (finalColor.a < 0.01) {
        discard;
    }

    outColor = finalColor;
}
