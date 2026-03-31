// motion_vectors.glsl
// Shared include for computing per-vertex screen-space motion vectors.
// Include this in forward pass fragment shaders that output to the motion vector MRT.
//
// Usage:
//   In vertex shader: pass currentClipPos and prevClipPos to fragment shader.
//   In fragment shader: call computeMotionVector() to get the MV output.
//
// Requires: prevViewProjection matrix available (push constant or UBO).

// Compute screen-space motion vector from current and previous clip-space positions.
// Returns motion in pixel coordinates (matching DLSS/FSR2 convention).
vec2 computeMotionVector(vec4 currentClipPos, vec4 prevClipPos)
{
    vec2 currentNDC = currentClipPos.xy / currentClipPos.w;
    vec2 prevNDC = prevClipPos.xy / prevClipPos.w;

    // NDC is [-1,1], motion vector in NDC space
    // DLSS expects motion vectors in pixel space, but Streamline can handle
    // NDC-space motion with mvecScale set to resolution.
    // We output in NDC space and set mvecScale in the upscaler.
    return currentNDC - prevNDC;
}

// For static geometry: compute motion purely from camera movement.
// worldPos is the vertex world position, shared between current and previous frame.
vec2 computeStaticMotionVector(vec3 worldPos, mat4 currentVP, mat4 prevVP)
{
    vec4 currentClip = currentVP * vec4(worldPos, 1.0);
    vec4 prevClip = prevVP * vec4(worldPos, 1.0);
    return computeMotionVector(currentClip, prevClip);
}
