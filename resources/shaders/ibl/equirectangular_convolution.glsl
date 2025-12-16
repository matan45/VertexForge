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

const float PI = 3.14159265359;
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
    // The world vector acts as the normal of a tangent surface
    // from the origin, aligned to WorldPos. Given this normal, calculate all
    // incoming radiance of the environment. The result of this radiance
    // is the radiance of light coming from -Normal direction, which is what
    // we use in the PBR shader to sample irradiance.
    vec3 N = normalize(WorldPos);

    vec3 irradiance = vec3(0.0);

    // Tangent space calculation from origin point
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    float sampleDelta = 0.025;
    float nrSamples = 0.0;

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // Spherical to cartesian (in tangent space)
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // Tangent space to world
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            // Sample from equirectangular map
            vec2 uv = SampleSphericalMap(sampleVec);
            irradiance += texture(equirectangularMap, uv).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / float(nrSamples));

    FragColor = vec4(irradiance, 1.0);
}