#type VERTEX
#version 460 core
layout (location = 0) in vec3 position;

layout(location = 0) out vec3 WorldPos;

layout(binding = 0) uniform UniformBufferObject {
    mat4 projection;
    mat4 view;
} ubo;

void main()
{
    WorldPos = position;
    gl_Position = ubo.projection * ubo.view * vec4(WorldPos, 1.0);
}

#type FRAGMENT
#version 460 core
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec3 WorldPos;

layout(binding = 1) uniform sampler2D equirectangularMap;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v)
{
    // Flip Y for Vulkan's coordinate system
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
