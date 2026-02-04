#type COMPUTE
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std430, set = 0, binding = 0) readonly buffer HeightmapIn
{
    float heights[];
};

layout(std430, set = 0, binding = 1) writeonly buffer InfluenceOut
{
    float influence[];
};

layout(push_constant) uniform PushConstants
{
    vec2 brushCenter;
    vec2 tileWorldOrigin;
    float brushRadius;
    float brushStrength;
    float vertexSpacing;
    uint verticesPerSide;
    uint falloffType;
    uint shapeType;
} pc;

float applyFalloff(float t, uint type)
{
    switch (type)
    {
        case 0: return 1.0;
        case 1: return 1.0 - t;
        case 2: return 1.0 - t * t * (3.0 - 2.0 * t);
        case 3: return 1.0 - t * t;
        default: return 0.0;
    }
}

void main()
{
    uint x = gl_GlobalInvocationID.x;
    uint z = gl_GlobalInvocationID.y;

    if (x >= pc.verticesPerSide || z >= pc.verticesPerSide)
    {
        return;
    }

    uint idx = z * pc.verticesPerSide + x;

    vec2 worldPos = pc.tileWorldOrigin + vec2(float(x), float(z)) * pc.vertexSpacing;
    vec2 delta = worldPos - pc.brushCenter;

    float dist;
    if (pc.shapeType == 0)
    {
        dist = length(delta) / pc.brushRadius;
    }
    else
    {
        dist = max(abs(delta.x), abs(delta.y)) / pc.brushRadius;
    }

    if (dist >= 1.0)
    {
        influence[idx] = 0.0;
        return;
    }

    influence[idx] = applyFalloff(dist, pc.falloffType) * pc.brushStrength;
}
