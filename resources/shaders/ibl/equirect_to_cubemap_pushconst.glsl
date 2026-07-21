// VK-1574 — Equirectangular HDR -> cube face, with the per-face viewProj supplied as a PUSH
// CONSTANT (not a UBO). Identical sampling math to equirectangular_to_cubemap.glsl (same Y-flip
// and SampleSphericalMap), but the push-constant transport lets all six faces record onto one
// command buffer without a per-face UBO race — the fenceless single-CB pattern used by
// HdrEnvironmentCapture (mirrors renderCubeFace in SkyEnvironmentCapture). The original UBO shader
// is left untouched because EnvironmentCubemapGenerator still uses it.

#type VERTEX
#version 460 core
layout (location = 0) in vec3 position;

layout(location = 0) out vec3 WorldPos;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

void main()
{
    WorldPos = position;
    gl_Position = pc.viewProj * vec4(WorldPos, 1.0);
}

#type FRAGMENT
#version 460 core
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec3 WorldPos;

layout(set = 0, binding = 0) uniform sampler2D equirectangularMap;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v)
{
    // Flip Y for Vulkan's coordinate system (matches equirectangular_to_cubemap.glsl)
    v.y *= -1.0;
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main()
{
    vec2 uv = SampleSphericalMap(normalize(WorldPos));
    vec3 color = texture(equirectangularMap, uv).rgb;

    FragColor = vec4(color, 1.0);
}
