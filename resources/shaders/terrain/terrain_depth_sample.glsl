#type COMPUTE
#version 450

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D depthSampler;

layout(std430, set = 0, binding = 1) writeonly buffer RaycastResult
{
    vec4 hitPosition;
    vec4 hitNormal;
};

layout(push_constant) uniform PushConstants
{
    mat4 invViewProjection;
    vec2 cursorUV;
    vec2 texelSize;
} pc;

vec3 unproject(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = pc.invViewProjection * ndc;
    return world.xyz / world.w;
}

void main()
{
    float dc = texture(depthSampler, pc.cursorUV).r;

    if (dc >= 1.0)
    {
        hitPosition = vec4(0.0, 0.0, 0.0, 0.0);
        hitNormal = vec4(0.0, 1.0, 0.0, 0.0);
        return;
    }

    vec3 worldPos = unproject(pc.cursorUV, dc);

    float dl = texture(depthSampler, pc.cursorUV + vec2(-pc.texelSize.x, 0)).r;
    float dr = texture(depthSampler, pc.cursorUV + vec2( pc.texelSize.x, 0)).r;
    float du = texture(depthSampler, pc.cursorUV + vec2(0, -pc.texelSize.y)).r;
    float dd = texture(depthSampler, pc.cursorUV + vec2(0,  pc.texelSize.y)).r;

    vec3 wl = unproject(pc.cursorUV + vec2(-pc.texelSize.x, 0), dl);
    vec3 wr = unproject(pc.cursorUV + vec2( pc.texelSize.x, 0), dr);
    vec3 wu = unproject(pc.cursorUV + vec2(0, -pc.texelSize.y), du);
    vec3 wd = unproject(pc.cursorUV + vec2(0,  pc.texelSize.y), dd);

    vec3 tangentX = wr - wl;
    vec3 tangentY = wd - wu;
    vec3 normal = normalize(cross(tangentX, tangentY));

    if (any(isnan(normal)) || length(normal) < 0.001)
    {
        normal = vec3(0.0, 1.0, 0.0);
    }

    hitPosition = vec4(worldPos, 1.0);
    hitNormal = vec4(normal, 0.0);
}
