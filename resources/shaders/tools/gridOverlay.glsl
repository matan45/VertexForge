#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 gridColor;
    vec4 axisColorX;
    vec4 axisColorZ;
    vec4 gridParams;  // x: gridSize, y: cellSize, z: fadeStart, w: fadeEnd
} pc;

layout(location = 0) out vec3 fragWorldPos;

void main() {
    fragWorldPos = inPosition;
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 gridColor;
    vec4 axisColorX;
    vec4 axisColorZ;
    vec4 gridParams;  // x: gridSize, y: cellSize, z: fadeStart, w: fadeEnd
} pc;

void main() {
    float cellSize = pc.gridParams.y;
    float fadeStart = pc.gridParams.z;
    float fadeEnd = pc.gridParams.w;

    // Distance-based fade (from camera origin in XZ plane)
    float dist = length(fragWorldPos.xz);
    float fade = 1.0 - smoothstep(fadeStart, fadeEnd, dist);

    // Tolerance for detecting axis lines (half cell size)
    float tolerance = cellSize * 0.1;

    // Check if this is the X-axis center line (Z near 0)
    bool isXAxis = abs(fragWorldPos.z) < tolerance;

    // Check if this is the Z-axis center line (X near 0)
    bool isZAxis = abs(fragWorldPos.x) < tolerance;

    // Check if this is a major grid line (every 10 cells)
    float majorInterval = cellSize * 10.0;
    bool isMajorX = abs(mod(abs(fragWorldPos.z) + tolerance, majorInterval)) < tolerance * 2.0;
    bool isMajorZ = abs(mod(abs(fragWorldPos.x) + tolerance, majorInterval)) < tolerance * 2.0;
    bool isMajor = isMajorX || isMajorZ;

    // Determine color based on line type
    vec4 color;
    if (isXAxis) {
        // X-axis line (red)
        color = pc.axisColorX;
    } else if (isZAxis) {
        // Z-axis line (blue)
        color = pc.axisColorZ;
    } else if (isMajor) {
        // Major grid line (brighter gray)
        color = pc.gridColor;
        color.rgb *= 1.5;
        color.a = min(color.a * 1.3, 1.0);
    } else {
        // Regular grid line
        color = pc.gridColor;
    }

    // Apply distance fade
    color.a *= fade;

    // Discard fully transparent pixels
    if (color.a < 0.01) {
        discard;
    }

    outColor = color;
}
