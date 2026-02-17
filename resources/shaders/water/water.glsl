#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

// Set 0: Camera (matches CameraUBO in MeshTypes.hpp, 240 bytes)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

// Set 1: Per-tile SSBO
struct WaterTileData {
    vec4 worldOriginAndSize;   // xyz = origin, w = tileSize
    vec4 heightAndWave;        // x = height, y = waveIntensity
};
layout(set = 1, binding = 0) readonly buffer TileBuffer {
    WaterTileData tiles[];
} tileData;

// Push constants (global water settings)
layout(push_constant) uniform PushConstants {
    vec4 shallowColor;
    vec4 deepColor;
    float waveSpeed;
    float waveAmplitude;
    float waveFrequency;
    float maxVisibleDepth;
    float fresnelPower;
    uint tileCount;
    uint subdivisions;
    float dudvTiling;
    float dudvStrength;
    float waveDirection;
} pc;

const float PI = 3.14159265358979;

// Rotate a 2D vector by angle (radians)
vec2 rotateDir(vec2 dir, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec2(c * dir.x - s * dir.y, s * dir.x + c * dir.y);
}

// Gerstner wave displacement
vec3 gerstnerWave(vec2 pos, vec2 dir, float steepness, float wavelength, float speed, float time) {
    float k = 2.0 * PI / wavelength;
    float c = speed / k;
    float a = steepness / k;
    vec2 d = normalize(dir);
    float f = k * (dot(d, pos) - c * time);
    return vec3(d.x * a * cos(f), a * sin(f), d.y * a * cos(f));
}

void main() {
    uint tileIndex = gl_InstanceIndex;
    vec4 originSize = tileData.tiles[tileIndex].worldOriginAndSize;
    vec4 heightWave = tileData.tiles[tileIndex].heightAndWave;

    vec3 tileOrigin = originSize.xyz;
    float tileSize = originSize.w;
    float waterHeight = heightWave.x;
    float waveIntensity = heightWave.y;

    // Scale unit quad to world tile
    vec3 worldPos = vec3(
        tileOrigin.x + inPosition.x * tileSize,
        waterHeight,
        tileOrigin.z + inPosition.z * tileSize
    );

    // Apply Gerstner waves (3 overlapping waves) with user-controlled direction
    float time = camera.u_Time * pc.waveSpeed;
    float amp = pc.waveAmplitude * waveIntensity;

    vec2 dir1 = rotateDir(vec2(1.0, 0.3), pc.waveDirection);
    vec2 dir2 = rotateDir(vec2(-0.4, 1.0), pc.waveDirection);
    vec2 dir3 = rotateDir(vec2(0.6, -0.8), pc.waveDirection);

    vec3 wave1 = gerstnerWave(worldPos.xz, dir1, 0.25 * amp, 8.0, 2.0, time);
    vec3 wave2 = gerstnerWave(worldPos.xz, dir2, 0.15 * amp, 5.0, 1.5, time);
    vec3 wave3 = gerstnerWave(worldPos.xz, dir3, 0.1 * amp, 12.0, 3.0, time);

    worldPos += wave1 + wave2 + wave3;

    // Compute normal via finite differences
    float eps = 0.1;
    vec2 baseXZ = worldPos.xz;

    vec3 posX = vec3(worldPos.x + eps, waterHeight, worldPos.z);
    posX += gerstnerWave(baseXZ + vec2(eps, 0.0), dir1, 0.25 * amp, 8.0, 2.0, time);
    posX += gerstnerWave(baseXZ + vec2(eps, 0.0), dir2, 0.15 * amp, 5.0, 1.5, time);
    posX += gerstnerWave(baseXZ + vec2(eps, 0.0), dir3, 0.1 * amp, 12.0, 3.0, time);

    vec3 posZ = vec3(worldPos.x, waterHeight, worldPos.z + eps);
    posZ += gerstnerWave(baseXZ + vec2(0.0, eps), dir1, 0.25 * amp, 8.0, 2.0, time);
    posZ += gerstnerWave(baseXZ + vec2(0.0, eps), dir2, 0.15 * amp, 5.0, 1.5, time);
    posZ += gerstnerWave(baseXZ + vec2(0.0, eps), dir3, 0.1 * amp, 12.0, 3.0, time);

    vec3 tangentX = posX - worldPos;
    vec3 tangentZ = posZ - worldPos;
    fragNormal = normalize(cross(tangentZ, tangentX));

    fragWorldPos = worldPos;
    fragTexCoord = inTexCoord;

    gl_Position = camera.projection * camera.view * vec4(worldPos, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

// IBL cubemaps (same bindings as mesh.glsl)
layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// Set 2: DuDv distortion texture
layout(set = 2, binding = 0) uniform sampler2D dudvMap;

layout(push_constant) uniform PushConstants {
    vec4 shallowColor;
    vec4 deepColor;
    float waveSpeed;
    float waveAmplitude;
    float waveFrequency;
    float maxVisibleDepth;
    float fresnelPower;
    uint tileCount;
    uint subdivisions;
    float dudvTiling;
    float dudvStrength;
    float waveDirection;
} pc;

void main() {
    vec3 N = normalize(fragNormal);

    // Animated dudv sampling (two layers scrolling in different directions)
    float moveSpeed = pc.waveSpeed * 0.03;
    vec2 dudvUV1 = fragTexCoord * pc.dudvTiling + vec2(camera.u_Time * moveSpeed);
    vec2 dudvUV2 = fragTexCoord * pc.dudvTiling * 0.8 + vec2(-camera.u_Time * moveSpeed * 0.7, camera.u_Time * moveSpeed * 0.5);

    vec2 distortion1 = texture(dudvMap, dudvUV1).rg * 2.0 - 1.0;
    vec2 distortion2 = texture(dudvMap, dudvUV2).rg * 2.0 - 1.0;
    vec2 totalDistortion = (distortion1 + distortion2) * pc.dudvStrength;

    // Perturb normal with dudv distortion
    N = normalize(N + vec3(totalDistortion.x, 0.0, totalDistortion.y));

    vec3 V = normalize(camera.cameraPos - fragWorldPos);
    vec3 R = reflect(-V, N);

    // Fresnel (Schlick approximation)
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NdotV, pc.fresnelPower);
    fresnel = clamp(fresnel, 0.0, 1.0);

    // Depth-based color blend using view angle as proxy
    float depthFactor = clamp(1.0 - NdotV, 0.0, 1.0);
    vec3 waterColor = mix(pc.shallowColor.rgb, pc.deepColor.rgb, depthFactor);

    // IBL reflection
    float roughness = 0.05; // Water is highly reflective
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * 4.0).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 F0 = vec3(0.02); // Water IOR ~1.33
    vec3 specular = prefilteredColor * (F0 * brdf.x + brdf.y);

    // Blend water color with reflection via Fresnel
    vec3 color = mix(waterColor, specular, fresnel);

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    // Alpha from shallow color alpha, modulated by Fresnel
    float alpha = mix(pc.shallowColor.a, 1.0, fresnel);

    outColor = vec4(color, alpha);
}
