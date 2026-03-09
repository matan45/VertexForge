// Shared wind functions for vegetation shaders
// Include via: #include "wind_common.glsl"

// Wind UBO should be bound by the including shader
// Expected layout:
// vec4 windDirectionAndSpeed;  // xyz=direction, w=speed
// vec4 windGustParams;         // x=gustStrength, y=gustFrequency, z=turbulenceScale, w=time

// Simple hash for pseudo-random based on position
float windHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// 2D noise for wind variation
float windNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = windHash(i);
    float b = windHash(i + vec2(1.0, 0.0));
    float c = windHash(i + vec2(0.0, 1.0));
    float d = windHash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Calculate wind displacement for a vertex
// vertexHeight: normalized height along blade/branch [0,1] - 0=root, 1=tip
// worldPos: world-space position of the vertex
vec3 calculateWindDisplacement(vec3 worldPos, float vertexHeight,
                                vec4 windDirSpeed, vec4 gustParams) {
    float windSpeed = windDirSpeed.w;
    vec3 windDir = windDirSpeed.xyz;
    float time = gustParams.w;
    float gustStrength = gustParams.x;
    float gustFreq = gustParams.y;
    float turbScale = gustParams.z;

    // Base wind sway
    float phase = dot(worldPos.xz, windDir.xz) * 0.5 + time * windSpeed;
    float baseSway = sin(phase) * 0.5 + 0.5;

    // Gust variation
    float gustPhase = time * gustFreq + windHash(worldPos.xz * 0.01);
    float gust = sin(gustPhase * 6.28318) * 0.5 + 0.5;
    gust = gust * gustStrength;

    // Spatial turbulence
    float turb = windNoise(worldPos.xz * turbScale + time * windSpeed * 0.3);

    // Combine - stronger at tip (vertexHeight^2 for quadratic falloff from root)
    float strength = (baseSway + gust) * (1.0 + turb * 0.5) * windSpeed;
    float heightFactor = vertexHeight * vertexHeight;

    vec3 displacement = windDir * strength * heightFactor;

    // Add slight perpendicular motion for natural look
    vec3 perpDir = vec3(-windDir.z, 0.0, windDir.x);
    float perpSway = sin(phase * 1.3 + 0.7) * 0.3;
    displacement += perpDir * perpSway * heightFactor * windSpeed;

    return displacement;
}
