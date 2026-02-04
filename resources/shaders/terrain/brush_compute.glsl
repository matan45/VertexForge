#type COMPUTE
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std430, set = 0, binding = 0) readonly buffer HeightmapIn
{
    float heightsIn[];
};

layout(std430, set = 0, binding = 1) writeonly buffer HeightmapOut
{
    float heightsOut[];
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
    uint brushType;     // 0=Raise, 1=Lower, 2=Smooth, 3=Flatten, 4=Noise
    float deltaTime;
    float targetHeight;
    float minHeight;
    float maxHeight;
    uint invertFlag;    // 0 or 1
} pc;

float applyFalloff(float t, uint type)
{
    switch (type)
    {
        case 0: return 1.0;                         // Constant
        case 1: return 1.0 - t;                     // Linear
        case 2: return 1.0 - t * t * (3.0 - 2.0 * t); // Smooth
        case 3: return 1.0 - t * t;                 // Sharp
        default: return 0.0;
    }
}

// Simple hash-based noise (deterministic, based on world position)
float hashNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n00 = fract(sin(dot(i, vec2(127.1, 311.7))) * 43758.5453);
    float n10 = fract(sin(dot(i + vec2(1.0, 0.0), vec2(127.1, 311.7))) * 43758.5453);
    float n01 = fract(sin(dot(i + vec2(0.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);
    float n11 = fract(sin(dot(i + vec2(1.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);

    float nx0 = mix(n00, n10, f.x);
    float nx1 = mix(n01, n11, f.x);
    return mix(nx0, nx1, f.y) * 2.0 - 1.0; // Range [-1, 1]
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
    float currentHeight = heightsIn[idx];

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

    // Outside brush radius: pass through
    if (dist >= 1.0)
    {
        heightsOut[idx] = currentHeight;
        return;
    }

    float influence = applyFalloff(dist, pc.falloffType);
    float newHeight = currentHeight;

    switch (pc.brushType)
    {
        case 0: // Raise
        {
            float direction = (pc.invertFlag != 0) ? -1.0 : 1.0;
            newHeight += direction * influence * pc.brushStrength * pc.deltaTime;
            break;
        }

        case 1: // Lower
        {
            float direction = (pc.invertFlag != 0) ? 1.0 : -1.0;
            newHeight += direction * influence * pc.brushStrength * pc.deltaTime;
            break;
        }

        case 2: // Smooth
        {
            // Average neighboring heights from input buffer
            float sum = 0.0;
            float count = 0.0;

            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (dx == 0 && dz == 0) continue;

                    int nx = int(x) + dx;
                    int nz = int(z) + dz;

                    if (nx >= 0 && nx < int(pc.verticesPerSide) &&
                        nz >= 0 && nz < int(pc.verticesPerSide))
                    {
                        sum += heightsIn[uint(nz) * pc.verticesPerSide + uint(nx)];
                        count += 1.0;
                    }
                }
            }

            if (count > 0.0)
            {
                float avgHeight = sum / count;
                float smoothFactor = influence * pc.brushStrength * pc.deltaTime;
                smoothFactor = clamp(smoothFactor, 0.0, 1.0);
                newHeight = mix(currentHeight, avgHeight, smoothFactor);
            }
            break;
        }

        case 3: // Flatten
        {
            float flattenFactor = influence * pc.brushStrength * pc.deltaTime;
            flattenFactor = clamp(flattenFactor, 0.0, 1.0);
            newHeight = mix(currentHeight, pc.targetHeight, flattenFactor);
            break;
        }

        case 4: // Noise
        {
            float direction = (pc.invertFlag != 0) ? -1.0 : 1.0;
            float noiseScale = 10.0;
            float noiseVal = hashNoise(worldPos * noiseScale);
            newHeight += direction * noiseVal * influence * pc.brushStrength * pc.deltaTime;
            break;
        }
    }

    heightsOut[idx] = clamp(newHeight, pc.minHeight, pc.maxHeight);
}
