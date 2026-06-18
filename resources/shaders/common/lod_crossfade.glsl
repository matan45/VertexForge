#ifndef LOD_CROSSFADE_GLSL
#define LOD_CROSSFADE_GLSL

// 4x4 Bayer dithering matrix for LOD crossfade transitions
const float bayerMatrix[16] = float[16](
     0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
     3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
);

// Compute crossfade alpha based on distance within transition zone
// Returns 0.0 at start of transition, 1.0 at end
float computeCrossfadeAlpha(float distanceSq, float lodThresholdSq, float transitionWidthSq) {
    float startSq = lodThresholdSq - transitionWidthSq;
    float endSq = lodThresholdSq;
    return clamp((distanceSq - startSq) / max(endSq - startSq, 0.001), 0.0, 1.0);
}

// Dither test - returns true if the pixel should be discarded
// crossfadeAlpha: 0.0 = fully visible, 1.0 = fully faded out
bool ditherTest(vec2 screenPos, float crossfadeAlpha) {
    if (crossfadeAlpha <= 0.0) return false;  // Fully visible
    if (crossfadeAlpha >= 1.0) return true;   // Fully faded

    ivec2 pos = ivec2(screenPos) % 4;
    int index = pos.y * 4 + pos.x;
    float threshold = bayerMatrix[index];

    return crossfadeAlpha > threshold;
}

// Extract crossfade alpha from the PerDrawData.lodLevel field.
// Layout (see gpu_types.glsl): bits 0-7 = LOD level, bits 8-15 = crossfade alpha
// as uint8 (0-255 mapped to 0.0-1.0). This must match where gpu_cull_lod.glsl
// packs the crossfade byte (lodLevel | crossfadeByte << 8).
float extractCrossfadeAlpha(uint packedLodLevel) {
    uint crossfadeByte = (packedLodLevel >> 8u) & 0xFFu;
    return float(crossfadeByte) / 255.0;
}

#endif // LOD_CROSSFADE_GLSL
