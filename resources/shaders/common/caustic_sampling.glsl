#ifndef CAUSTIC_SAMPLING_GLSL
#define CAUSTIC_SAMPLING_GLSL

float sampleCaustics(sampler2D causticMap, float waterHeight, float causticStrength,
                     float depthFalloff, float patchSize,
                     vec3 worldPos, vec3 lightDir) {
    float depth = waterHeight - worldPos.y;
    if (depth <= 0.0) return 0.0;

    // Project world position along light direction to water surface plane
    float t = depth / max(-lightDir.y, 0.001);
    vec3 surfaceHit = worldPos - lightDir * t;

    // Sample caustic texture (tiles with ocean patch)
    vec2 causticUV = surfaceHit.xz / patchSize;
    float intensity = texture(causticMap, causticUV).r;

    // Depth-based exponential falloff
    float falloff = exp(-depth * depthFalloff);

    return intensity * causticStrength * falloff;
}

#endif
