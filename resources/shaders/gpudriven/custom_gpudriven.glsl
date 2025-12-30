#type VERTEX
#version 460 core

// GPU-Driven custom vertex shader

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out flat uint fragDrawIndex;

// Set 0: Camera UBO (matches GPU-driven layout)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

// Per-draw data structure (must match GPUDrivenTypes.hpp PerDrawData)
struct PerDrawData {
    mat4 modelMatrix;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 materialParams;
    uvec4 textureIndices0;
    uvec4 textureIndices1;
    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;
    uint lodLevel;
    uint shaderGroupIndex;
    uint padding1;
    uint padding2;
};

// Set 1: Per-draw data buffer
layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

void main() {
    // Get draw index from gl_BaseInstance (set by indirect draw command)
    uint drawIndex = gl_BaseInstance;
    PerDrawData drawData = perDrawData[drawIndex];

    vec4 worldPos = drawData.modelMatrix * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // Transform normal using pre-computed normal matrix
    fragNormal = normalize(mat3(drawData.normalMatrix) * inNormal);

    fragTexCoord = inTexCoord;
    fragDrawIndex = drawIndex;

    gl_Position = camera.projection * camera.view * worldPos;
}

#type FRAGMENT
#version 460 core

// GPU-Driven custom fragment shader

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in flat uint fragDrawIndex;

layout(location = 0) out vec4 outColor;

// Set 0: Camera and IBL resources
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// Per-draw data structure (must match GPUDrivenTypes.hpp PerDrawData)
struct PerDrawData {
    mat4 modelMatrix;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 materialParams;
    uvec4 textureIndices0;
    uvec4 textureIndices1;
    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;
    uint lodLevel;
    uint shaderGroupIndex;
    uint padding1;
    uint padding2;
};

// Set 1: Per-draw data buffer
layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

// Set 2: Bindless texture array
layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

const uint INVALID_TEXTURE_INDEX = 0xFFFFFFFF;

bool isValidTexture(uint index) {
    return index != INVALID_TEXTURE_INDEX && index != 0xFFu && index < 4096u;
}

void main() {
    PerDrawData drawData = perDrawData[fragDrawIndex];

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    // Get texture indices
    uint albedoIdx = drawData.textureIndices0.x;

    // Apply time-based UV animation (for materials with Time node)
    vec2 uv = fragTexCoord + vec2(camera.u_Time * 0.1, 0.0);

    // Sample albedo (texture is in sRGB space)
    vec4 albedoSample = vec4(drawData.albedo.rgb, 1.0);
    if (isValidTexture(albedoIdx)) {
        albedoSample = texture(bindlessTextures[nonuniformEXT(albedoIdx)], uv);
    }

    // Output texture color directly (already in sRGB space)
    vec3 color = albedoSample.rgb;

    outColor = vec4(color, albedoSample.a);
}
