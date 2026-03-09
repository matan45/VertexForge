#type COMPUTE
#version 460
#extension GL_EXT_shader_explicit_arithmetic_types : enable

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Density map for this tile (flat array, resolution*resolution)
layout(std430, set = 0, binding = 0) readonly buffer DensityMapBuffer {
    float densityValues[];
};

// Terrain height data for this tile
layout(std430, set = 0, binding = 1) readonly buffer HeightMapBuffer {
    float heightValues[];
};

// Hole mask for this tile (0=solid, 1=hole, per quad)
layout(std430, set = 0, binding = 2) readonly buffer HoleMaskBuffer {
    uint holeMask[];
};

// Output grass instances
layout(std430, set = 0, binding = 3) buffer GrassInstanceBuffer {
    vec4 grassInstances[];  // Packed: [posAndRot, scaleAndDensity, color] per instance
};

// Atomic counter for output instances
layout(std430, set = 0, binding = 4) buffer CounterBuffer {
    uint instanceCount;
};

// Push constants
layout(push_constant) uniform PushConstants {
    vec2 tileWorldOrigin;
    float tileWorldSize;
    float vertexSpacing;
    uint verticesPerSide;
    uint maxInstances;
    float slopeLimit;       // dot(normal, up) threshold
    float densityMultiplier;
    float heightMin;
    float heightMax;
    float widthMin;
    float widthMax;
    float time;
};

// Hash for pseudo-random
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(269.5, 183.3))) * 43758.5453123);
}

vec3 getTerrainNormal(uint x, uint z) {
    // Compute normal from height differences
    float hC = heightValues[z * verticesPerSide + x];
    float hR = (x + 1 < verticesPerSide) ? heightValues[z * verticesPerSide + x + 1] : hC;
    float hU = (z + 1 < verticesPerSide) ? heightValues[(z + 1) * verticesPerSide + x] : hC;
    float hL = (x > 0) ? heightValues[z * verticesPerSide + x - 1] : hC;
    float hD = (z > 0) ? heightValues[(z - 1) * verticesPerSide + x] : hC;

    vec3 normal = vec3(hL - hR, 2.0 * vertexSpacing, hD - hU);
    return normalize(normal);
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    uint totalTexels = verticesPerSide * verticesPerSide;
    if (idx >= totalTexels) return;

    uint tx = idx % verticesPerSide;
    uint tz = idx / verticesPerSide;

    // Read density
    float density = densityValues[idx] * densityMultiplier;
    if (density <= 0.001) return;

    // Check hole mask (per quad, not per vertex)
    uint quadCount = verticesPerSide - 1;
    uint qx = min(tx, quadCount - 1);
    uint qz = min(tz, quadCount - 1);
    if (holeMask[qz * quadCount + qx] != 0) return;

    // Check slope
    vec3 normal = getTerrainNormal(tx, tz);
    float slopeDot = normal.y;  // dot with up
    if (slopeDot < slopeLimit) return;

    // Generate blade(s) at this texel - number based on density
    float worldX = tileWorldOrigin.x + float(tx) * vertexSpacing;
    float worldZ = tileWorldOrigin.y + float(tz) * vertexSpacing;
    float height = heightValues[idx];

    // Up to 4 blades per texel based on density
    uint bladeCount = uint(ceil(density * 4.0));

    for (uint b = 0; b < bladeCount; b++) {
        // Jitter position within cell
        vec2 seed = vec2(worldX, worldZ) + vec2(float(b) * 13.7, float(b) * 7.3);
        float jitterX = (hash(seed) - 0.5) * vertexSpacing;
        float jitterZ = (hash2(seed) - 0.5) * vertexSpacing;

        float bladeX = worldX + jitterX;
        float bladeZ = worldZ + jitterZ;

        // Interpolate height at jittered position
        float bladeHeight = height; // Simplified - use center height

        // Random rotation
        float rotation = hash(seed * 2.7) * 6.28318;

        // Random scale within range
        float scaleFactor = hash(seed * 3.1);
        float bladeH = mix(heightMin, heightMax, scaleFactor);
        float bladeW = mix(widthMin, widthMax, scaleFactor);

        // Wind phase offset based on position
        float windPhase = hash(seed * 5.3);

        // Allocate output slot
        uint outIdx = atomicAdd(instanceCount, 1);
        if (outIdx >= maxInstances) {
            atomicAdd(instanceCount, uint(-1)); // Undo
            return;
        }

        // Write 3 vec4s per instance (matches GrassInstanceGPU)
        uint base = outIdx * 3;
        grassInstances[base + 0] = vec4(bladeX, bladeHeight, bladeZ, rotation);
        grassInstances[base + 1] = vec4(bladeH, bladeW, density, windPhase);
        grassInstances[base + 2] = vec4(1.0, 1.0, 1.0, 1.0); // Color tint (white = use material)
    }
}
