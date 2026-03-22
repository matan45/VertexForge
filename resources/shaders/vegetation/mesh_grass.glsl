#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 12, max_primitives = 6) out;

// Same bindings as task shader
layout(std430, set = 0, binding = 0) readonly buffer GrassInstanceBuffer {
    vec4 grassInstances[];
};

// Camera — matches CameraUBO (240 bytes): view, projection, cameraPos, time, frustumPlanes[6]
layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    vec4 frustumPlanes[6];
};

layout(set = 2, binding = 0) uniform WindUBO {
    vec4 windDirectionAndSpeed;
    vec4 windGustParams;
};

layout(push_constant) uniform PushConstants {
    vec4 baseColor;
    vec4 tipColor;
    float fadeStartDistance;
    float fadeEndDistance;
    float sssDistortion;
    float sssPower;
    float sssScale;
    uint billboardTextureIndex;
};

struct GrassPayload {
    uint instanceIndices[32];
    float distanceToCamera[32];
};

taskPayloadSharedEXT GrassPayload payload;

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

// Emit a vertex with rotation, wind, and projection applied
void emitVertex(uint idx, vec3 rootPos, vec3 localOffset, float height, float bladeWidth,
                float cosR, float sinR, float windPhase, float alpha, vec3 bladeNormal) {
    vec3 rotated = vec3(
        localOffset.x * cosR - localOffset.z * sinR,
        localOffset.y,
        localOffset.x * sinR + localOffset.z * cosR
    );

    vec3 windDisp = calculateWindDisp(rootPos, height, windDirectionAndSpeed,
                                       vec4(windGustParams.xyz, windGustParams.w + windPhase));

    vec3 worldP = rootPos + rotated + windDisp;
    vec3 normal = normalize(mix(bladeNormal, vec3(0.0, 1.0, 0.0), height * 0.4));

    gl_MeshVerticesEXT[idx].gl_Position = projection * view * vec4(worldP, 1.0);
    outWorldPos[idx] = worldP;
    outNormal[idx] = normal;
    outUV[idx] = vec2(localOffset.x / max(bladeWidth, 0.001) + 0.5, height);
    outAlpha[idx] = alpha;
}

void generateGrassBlade(vec3 rootPos, float bladeHeight, float bladeWidth,
                         float cosR, float sinR, float windPhase, float alpha) {
    float halfW = bladeWidth * 0.5;
    float midW = halfW * 0.5;
    vec3 bladeNormal = normalize(vec3(sinR, 0.0, cosR));

    SetMeshOutputsEXT(7, 5);

    emitVertex(0, rootPos, vec3(-halfW, 0.0, 0.0), 0.0, bladeWidth, cosR, sinR, windPhase, alpha, bladeNormal);
    emitVertex(1, rootPos, vec3( halfW, 0.0, 0.0), 0.0, bladeWidth, cosR, sinR, windPhase, alpha, bladeNormal);
    emitVertex(2, rootPos, vec3(-midW, bladeHeight * 0.33, 0.0), 0.33, bladeWidth, cosR, sinR, windPhase, alpha, bladeNormal);
    emitVertex(3, rootPos, vec3( midW, bladeHeight * 0.33, 0.0), 0.33, bladeWidth, cosR, sinR, windPhase, alpha, bladeNormal);
    emitVertex(4, rootPos, vec3(-midW * 0.5, bladeHeight * 0.66, 0.0), 0.66, bladeWidth, cosR, sinR, windPhase, alpha, bladeNormal);
    emitVertex(5, rootPos, vec3( midW * 0.5, bladeHeight * 0.66, 0.0), 0.66, bladeWidth, cosR, sinR, windPhase, alpha, bladeNormal);
    emitVertex(6, rootPos, vec3(0.0, bladeHeight, 0.0), 1.0, bladeWidth, cosR, sinR, windPhase, alpha, bladeNormal);

    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
    gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1, 3, 2);
    gl_PrimitiveTriangleIndicesEXT[2] = uvec3(2, 3, 4);
    gl_PrimitiveTriangleIndicesEXT[3] = uvec3(3, 5, 4);
    gl_PrimitiveTriangleIndicesEXT[4] = uvec3(4, 5, 6);
}

void generateFlowerCross(vec3 rootPos, float height, float width,
                          float cosR, float sinR, float windPhase, float alpha) {
    // Cross-billboard: two perpendicular quads forming an X shape
    float halfW = width * 0.5;
    vec3 normalA = normalize(vec3(sinR, 0.0, cosR));
    vec3 normalB = normalize(vec3(cosR, 0.0, -sinR));

    SetMeshOutputsEXT(8, 4);

    // Quad A (aligned with blade rotation)
    emitVertex(0, rootPos, vec3(-halfW, 0.0, 0.0), 0.0, width, cosR, sinR, windPhase, alpha, normalA);
    emitVertex(1, rootPos, vec3( halfW, 0.0, 0.0), 0.0, width, cosR, sinR, windPhase, alpha, normalA);
    emitVertex(2, rootPos, vec3(-halfW, height, 0.0), 1.0, width, cosR, sinR, windPhase, alpha, normalA);
    emitVertex(3, rootPos, vec3( halfW, height, 0.0), 1.0, width, cosR, sinR, windPhase, alpha, normalA);

    // Quad B (perpendicular to blade rotation)
    float cosR90 = -sinR;
    float sinR90 = cosR;
    emitVertex(4, rootPos, vec3(-halfW, 0.0, 0.0), 0.0, width, cosR90, sinR90, windPhase, alpha, normalB);
    emitVertex(5, rootPos, vec3( halfW, 0.0, 0.0), 0.0, width, cosR90, sinR90, windPhase, alpha, normalB);
    emitVertex(6, rootPos, vec3(-halfW, height, 0.0), 1.0, width, cosR90, sinR90, windPhase, alpha, normalB);
    emitVertex(7, rootPos, vec3( halfW, height, 0.0), 1.0, width, cosR90, sinR90, windPhase, alpha, normalB);

    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
    gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1, 3, 2);
    gl_PrimitiveTriangleIndicesEXT[2] = uvec3(4, 5, 6);
    gl_PrimitiveTriangleIndicesEXT[3] = uvec3(5, 7, 6);
}

void generateBushCross(vec3 rootPos, float height, float width,
                        float cosR, float sinR, float windPhase, float alpha) {
    // 3 intersecting quads at 60-degree intervals for volumetric bush look
    float halfW = width * 0.5;
    float baseAngle = atan(sinR, cosR);

    SetMeshOutputsEXT(12, 6);

    // 3 quads at 0, 60, 120 degrees
    for (uint q = 0; q < 3; q++) {
        float totalAngle = baseAngle + float(q) * 1.0472; // 60 degrees
        float cr = cos(totalAngle);
        float sr = sin(totalAngle);
        vec3 normal = normalize(vec3(sr, 0.0, cr));

        uint b = q * 4;
        emitVertex(b + 0, rootPos, vec3(-halfW, 0.0, 0.0), 0.0, width, cr, sr, windPhase, alpha, normal);
        emitVertex(b + 1, rootPos, vec3( halfW, 0.0, 0.0), 0.0, width, cr, sr, windPhase, alpha, normal);
        emitVertex(b + 2, rootPos, vec3(-halfW, height, 0.0), 1.0, width, cr, sr, windPhase, alpha, normal);
        emitVertex(b + 3, rootPos, vec3( halfW, height, 0.0), 1.0, width, cr, sr, windPhase, alpha, normal);

        uint triBase = q * 2;
        gl_PrimitiveTriangleIndicesEXT[triBase + 0] = uvec3(b + 0, b + 1, b + 2);
        gl_PrimitiveTriangleIndicesEXT[triBase + 1] = uvec3(b + 1, b + 3, b + 2);
    }
}

void generateBillboard(vec3 rootPos, float height, float width,
                        float windPhase, float alpha) {
    // Camera-facing billboard quad (4 vertices, 2 triangles)
    vec3 toCamera = normalize(cameraPos - rootPos);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, toCamera));

    float halfW = width * 0.5;
    vec3 faceNormal = toCamera;

    SetMeshOutputsEXT(4, 2);

    // Bottom-left
    vec3 windDisp0 = calculateWindDisp(rootPos, 0.0, windDirectionAndSpeed,
                                        vec4(windGustParams.xyz, windGustParams.w + windPhase));
    vec3 p0 = rootPos - right * halfW + windDisp0;
    gl_MeshVerticesEXT[0].gl_Position = projection * view * vec4(p0, 1.0);
    outWorldPos[0] = p0;
    outNormal[0] = faceNormal;
    outUV[0] = vec2(0.0, 0.0);
    outAlpha[0] = alpha;

    // Bottom-right
    vec3 p1 = rootPos + right * halfW + windDisp0;
    gl_MeshVerticesEXT[1].gl_Position = projection * view * vec4(p1, 1.0);
    outWorldPos[1] = p1;
    outNormal[1] = faceNormal;
    outUV[1] = vec2(1.0, 0.0);
    outAlpha[1] = alpha;

    // Top-left
    vec3 windDisp1 = calculateWindDisp(rootPos, 1.0, windDirectionAndSpeed,
                                        vec4(windGustParams.xyz, windGustParams.w + windPhase));
    vec3 p2 = rootPos - right * halfW + up * height + windDisp1;
    gl_MeshVerticesEXT[2].gl_Position = projection * view * vec4(p2, 1.0);
    outWorldPos[2] = p2;
    outNormal[2] = faceNormal;
    outUV[2] = vec2(0.0, 1.0);
    outAlpha[2] = alpha;

    // Top-right
    vec3 p3 = rootPos + right * halfW + up * height + windDisp1;
    gl_MeshVerticesEXT[3].gl_Position = projection * view * vec4(p3, 1.0);
    outWorldPos[3] = p3;
    outNormal[3] = faceNormal;
    outUV[3] = vec2(1.0, 1.0);
    outAlpha[3] = alpha;

    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
    gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1, 3, 2);
}

void main() {
    uint gid = gl_WorkGroupID.x;

    uint instanceIdx = payload.instanceIndices[gid];
    float dist = payload.distanceToCamera[gid];

    uint base = instanceIdx * 3;
    vec4 posAndRot = grassInstances[base + 0];
    vec4 scaleAndDensity = grassInstances[base + 1];
    vec4 colorTint = grassInstances[base + 2];

    vec3 rootPos = posAndRot.xyz;
    float rotation = posAndRot.w;
    float bladeHeight = scaleAndDensity.x;
    float bladeWidth = scaleAndDensity.y;
    float windPhase = scaleAndDensity.w;
    uint vegType = uint(colorTint.w);

    float alpha = 1.0;
    if (dist > fadeStartDistance) {
        alpha = 1.0 - (dist - fadeStartDistance) / (fadeEndDistance - fadeStartDistance);
        alpha = clamp(alpha, 0.0, 1.0);
    }

    float cosR = cos(rotation);
    float sinR = sin(rotation);

    if (vegType == 0u) {
        generateGrassBlade(rootPos, bladeHeight, bladeWidth, cosR, sinR, windPhase, alpha);
    } else if (vegType == 1u) {
        generateFlowerCross(rootPos, bladeHeight, bladeWidth * 2.0, cosR, sinR, windPhase, alpha);
    } else if (vegType == 2u) {
        generateBushCross(rootPos, bladeHeight * 1.5, bladeWidth * 3.0, cosR, sinR, windPhase, alpha);
    } else {
        // Billboard: camera-facing quad
        generateBillboard(rootPos, bladeHeight, bladeWidth * 2.0, windPhase, alpha);
    }
}
