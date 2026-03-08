#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 7, max_primitives = 5) out;

// Same bindings as task shader
layout(std430, set = 0, binding = 0) readonly buffer GrassInstanceBuffer {
    vec4 grassInstances[];
};

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
    float time;
};

// Wind UBO
layout(set = 2, binding = 0) uniform WindUBO {
    vec4 windDirectionAndSpeed;
    vec4 windGustParams;
};

layout(push_constant) uniform PushConstants {
    float fadeStartDistance;
    float fadeEndDistance;
};

struct GrassPayload {
    uint instanceIndices[32];
    float distanceToCamera[32];
};

taskPayloadSharedEXT GrassPayload payload;

// Outputs
layout(location = 0) out vec3 outWorldPos[];
layout(location = 1) out vec3 outNormal[];
layout(location = 2) out vec2 outUV[];
layout(location = 3) out float outAlpha[];

// Wind functions inlined (matches wind_common.glsl logic)
float windHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec3 calculateWindDisp(vec3 worldPos, float vertexHeight, vec4 windDirSpeed, vec4 gustParams) {
    float windSpeed = windDirSpeed.w;
    vec3 windDir = windDirSpeed.xyz;
    float t = gustParams.w;
    float gustStr = gustParams.x;
    float gustFreq = gustParams.y;

    float phase = dot(worldPos.xz, windDir.xz) * 0.5 + t * windSpeed;
    float baseSway = sin(phase) * 0.5 + 0.5;
    float gust = sin(t * gustFreq * 6.28318 + windHash(worldPos.xz * 0.01) * 6.28318) * 0.5 + 0.5;

    float strength = (baseSway + gust * gustStr) * windSpeed;
    float hFactor = vertexHeight * vertexHeight;

    vec3 disp = windDir * strength * hFactor;
    vec3 perpDir = vec3(-windDir.z, 0.0, windDir.x);
    disp += perpDir * sin(phase * 1.3 + 0.7) * 0.3 * hFactor * windSpeed;

    return disp;
}

void main() {
    uint gid = gl_WorkGroupID.x;

    uint instanceIdx = payload.instanceIndices[gid];
    float dist = payload.distanceToCamera[gid];

    // Read instance data
    uint base = instanceIdx * 3;
    vec4 posAndRot = grassInstances[base + 0];
    vec4 scaleAndDensity = grassInstances[base + 1];
    vec4 colorTint = grassInstances[base + 2];

    vec3 rootPos = posAndRot.xyz;
    float rotation = posAndRot.w;
    float bladeHeight = scaleAndDensity.x;
    float bladeWidth = scaleAndDensity.y;
    float windPhase = scaleAndDensity.w;

    // Distance fade
    float alpha = 1.0;
    if (dist > fadeStartDistance) {
        alpha = 1.0 - (dist - fadeStartDistance) / (fadeEndDistance - fadeStartDistance);
        alpha = clamp(alpha, 0.0, 1.0);
    }

    // Rotation matrix (Y-axis)
    float cosR = cos(rotation);
    float sinR = sin(rotation);

    // Generate blade vertices (7 vertices, 5 triangles = tapered blade)
    // v0,v1 = base, v2,v3 = lower mid, v4,v5 = upper mid, v6 = tip
    float halfW = bladeWidth * 0.5;
    float midW = halfW * 0.5;

    vec3 offsets[7];
    float heights[7];

    // Base (0.0)
    offsets[0] = vec3(-halfW, 0.0, 0.0);  heights[0] = 0.0;
    offsets[1] = vec3( halfW, 0.0, 0.0);  heights[1] = 0.0;
    // Lower mid (0.33)
    offsets[2] = vec3(-midW, bladeHeight * 0.33, 0.0);  heights[2] = 0.33;
    offsets[3] = vec3( midW, bladeHeight * 0.33, 0.0);  heights[3] = 0.33;
    // Upper mid (0.66)
    offsets[4] = vec3(-midW * 0.5, bladeHeight * 0.66, 0.0); heights[4] = 0.66;
    offsets[5] = vec3( midW * 0.5, bladeHeight * 0.66, 0.0); heights[5] = 0.66;
    // Tip (1.0)
    offsets[6] = vec3(0.0, bladeHeight, 0.0);  heights[6] = 1.0;

    // Apply rotation and wind, emit vertices
    SetMeshOutputsEXT(7, 5);

    for (uint v = 0; v < 7; v++) {
        vec3 off = offsets[v];
        // Rotate around Y
        vec3 rotated = vec3(
            off.x * cosR - off.z * sinR,
            off.y,
            off.x * sinR + off.z * cosR
        );

        // Apply wind
        vec3 windDisp = calculateWindDisp(rootPos, heights[v], windDirectionAndSpeed,
                                           vec4(windGustParams.xyz, windGustParams.w + windPhase));

        vec3 worldP = rootPos + rotated + windDisp;

        gl_MeshVerticesEXT[v].gl_Position = projection * view * vec4(worldP, 1.0);
        outWorldPos[v] = worldP;
        outNormal[v] = vec3(0.0, 1.0, 0.0); // Simplified normal
        outUV[v] = vec2(off.x / bladeWidth + 0.5, heights[v]);
        outAlpha[v] = alpha;
    }

    // 5 triangles forming the blade
    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
    gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1, 3, 2);
    gl_PrimitiveTriangleIndicesEXT[2] = uvec3(2, 3, 4);
    gl_PrimitiveTriangleIndicesEXT[3] = uvec3(3, 5, 4);
    gl_PrimitiveTriangleIndicesEXT[4] = uvec3(4, 5, 6);
}
