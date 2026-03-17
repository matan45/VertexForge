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

void main()
{
    // Use vertex position directly as cubemap sampling direction
    vec3 dir = normalize(localPos);

    vec3 envColor = texture(environmentMap, dir).rgb;
    FragColor = vec4(envColor, 1.0);

}