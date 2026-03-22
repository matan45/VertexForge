#type COMPUTE
#version 460
#extension GL_EXT_shader_explicit_arithmetic_types : enable

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Density map for this tile (flat array, resolution*resolution)
layout(std430, set = 0, binding = 0) readonly buffer DensityMapBuffer {
    float densityValues[];
};

layout(std430, set = 0, binding = 1) readonly buffer HeightMapBuffer {
    float heightValues[];
};

// Hole mask for this tile (0=solid, 1=hole, per quad)
layout(std430, set = 0, binding = 2) readonly buffer HoleMaskBuffer {
    uint holeMask[];
};

layout(std430, set = 0, binding = 3) buffer GrassInstanceBuffer {
    vec4 grassInstances[];  // Packed: [posAndRot, scaleAndDensity, color] per instance
};

layout(std430, set = 0, binding = 4) buffer CounterBuffer {
    uint instanceCount;
};

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
    // Distance-based density fadeout
    float cameraX;
    float cameraZ;
    float densityFadeStart;
    float densityFadeEnd;
    float minDensityScale;
    // Multi-type vegetation
    uint vegetationType;         // 0=Grass, 1=Billboard
    uint billboardTextureIndex;  // Bindless texture index for this dispatch
    uint billboardMode;          // 0=Cross, 1=CameraFacing
    uint paletteEntryIndex;      // Which palette entry this dispatch is for
    uint paletteEntryCount;      // Total entries in palette
    float entryScaleMin;         // Per-entry scale range
    float entryScaleMax;
};

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(269.5, 183.3))) * 43758.5453123);
}

vec3 getTerrainNormal(uint x, uint z) {
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

    float density = densityValues[idx] * densityMultiplier;
    if (density <= 0.001) return;

    // Check hole mask (per quad, not per vertex)
    uint quadCount = verticesPerSide - 1;
    uint qx = min(tx, quadCount - 1);
    uint qz = min(tz, quadCount - 1);
    if (holeMask[qz * quadCount + qx] != 0) return;

    vec3 normal = getTerrainNormal(tx, tz);
    float slopeDot = normal.y;  // dot with up
    if (slopeDot < slopeLimit) return;

    float worldX = tileWorldOrigin.x + float(tx) * vertexSpacing;
    float worldZ = tileWorldOrigin.y + float(tz) * vertexSpacing;
    float height = heightValues[idx];

    // Distance-based density reduction (hash-based consistent culling)
    float dx = worldX - cameraX;
    float dz = worldZ - cameraZ;
    float distSq = dx * dx + dz * dz;
    float fadeStartSq = densityFadeStart * densityFadeStart;
    float fadeEndSq = densityFadeEnd * densityFadeEnd;
    float densityScale = 1.0;
    if (distSq > fadeStartSq) {
        float t = clamp((distSq - fadeStartSq) / (fadeEndSq - fadeStartSq), 0.0, 1.0);
        densityScale = mix(1.0, minDensityScale, t);
    }

    uint bladeCount = uint(ceil(density * 4.0));

    for (uint b = 0; b < bladeCount; b++) {
        vec2 seed = vec2(worldX, worldZ) + vec2(float(b) * 13.7, float(b) * 7.3);

        // Deterministic hash per blade position -- no popping as camera moves
        float survivalHash = hash(seed * 11.3);
        if (survivalHash >= densityScale) continue;

        // For billboard palette: assign each blade to exactly one entry via hash
        if (paletteEntryCount > 1u) {
            uint assignedEntry = uint(hash(seed * 17.3) * float(paletteEntryCount));
            assignedEntry = min(assignedEntry, paletteEntryCount - 1u);
            if (assignedEntry != paletteEntryIndex) continue;
        }

        float jitterX = (hash(seed) - 0.5) * vertexSpacing;
        float jitterZ = (hash2(seed) - 0.5) * vertexSpacing;

        float bladeX = worldX + jitterX;
        float bladeZ = worldZ + jitterZ;

        float bladeHeight = height;

        float rotation = hash(seed * 2.7) * 6.28318;

        float scaleFactor = hash(seed * 3.1);
        // Use per-entry scale range if available, otherwise fall back to global config
        float scaleMin = (entryScaleMin > 0.0) ? entryScaleMin : heightMin;
        float scaleMax = (entryScaleMax > 0.0) ? entryScaleMax : heightMax;
        float bladeH = mix(scaleMin, scaleMax, scaleFactor);
        float bladeW = mix(widthMin, widthMax, scaleFactor);

        float windPhase = hash(seed * 5.3);

        uint outIdx = atomicAdd(instanceCount, 1);
        if (outIdx >= maxInstances) {
            atomicAdd(instanceCount, uint(-1)); // Undo
            return;
        }

        // Write 3 vec4s per instance (matches GrassInstanceGPU)
        // color.x = bindless texture index (as float bits)
        // color.y = billboard mode (as float: 0=Cross, 1=CameraFacing)
        // color.w = vegetation type
        uint base = outIdx * 3;
        grassInstances[base + 0] = vec4(bladeX, bladeHeight, bladeZ, rotation);
        grassInstances[base + 1] = vec4(bladeH, bladeW, density, windPhase);
        // Store texture index as float (not uintBitsToFloat - denormals get flushed to zero on GPU)
        grassInstances[base + 2] = vec4(float(billboardTextureIndex), float(billboardMode), 1.0, float(vegetationType));
    }
}
