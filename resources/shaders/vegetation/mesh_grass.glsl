#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 8, max_primitives = 4) out;

layout(std430, set = 0, binding = 0) readonly buffer GrassInstanceBuffer {
    vec4 grassInstances[];
};

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
layout(location = 4) flat out uint outVegType[];
layout(location = 5) flat out uint outTexIndex[];

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

// Per-instance data shared across writeVertex calls
uint gVegType;
uint gTexIndex;

// Write one vertex with rotation + wind applied
void writeVertex(uint idx, vec3 rootPos, vec3 localOff, float h, float w,
                 float cr, float sr, float wp, float a, vec3 n) {
    vec3 rotated = vec3(localOff.x * cr - localOff.z * sr, localOff.y, localOff.x * sr + localOff.z * cr);
    vec3 wd = calculateWindDisp(rootPos, h, windDirectionAndSpeed, vec4(windGustParams.xyz, windGustParams.w + wp));
    vec3 worldP = rootPos + rotated + wd;
    vec3 normal = normalize(mix(n, vec3(0.0, 1.0, 0.0), h * 0.4));

    gl_MeshVerticesEXT[idx].gl_Position = projection * view * vec4(worldP, 1.0);
    outWorldPos[idx] = worldP;
    outNormal[idx] = normal;
    outUV[idx] = vec2(localOff.x / max(w, 0.001) + 0.5, h);
    outAlpha[idx] = a;
    outVegType[idx] = gVegType;
    outTexIndex[idx] = gTexIndex;
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
    float rot = posAndRot.w;
    float bH = scaleAndDensity.x;
    float bW = scaleAndDensity.y;
    float wp = scaleAndDensity.w;
    uint vt = uint(colorTint.w);

    float alpha = 1.0;
    if (dist > fadeStartDistance) {
        alpha = 1.0 - (dist - fadeStartDistance) / (fadeEndDistance - fadeStartDistance);
        alpha = clamp(alpha, 0.0, 1.0);
    }

    float cr = cos(rot);
    float sr = sin(rot);

    gVegType = vt;
    gTexIndex = floatBitsToUint(colorTint.x); // Texture index packed by compute shader
    uint bbMode = uint(colorTint.y);          // 0=Cross, 1=CameraFacing

    if (vt == 1u && bbMode == 1u) {
        // Billboard: camera-facing quad
        vec3 toCamera = normalize(cameraPos - rootPos);
        vec3 up = vec3(0.0, 1.0, 0.0);
        vec3 right = normalize(cross(up, toCamera));
        float hw = bW;
        vec3 fn = toCamera;

        vec3 wd0 = calculateWindDisp(rootPos, 0.0, windDirectionAndSpeed, vec4(windGustParams.xyz, windGustParams.w + wp));
        vec3 wd1 = calculateWindDisp(rootPos, 1.0, windDirectionAndSpeed, vec4(windGustParams.xyz, windGustParams.w + wp));

        vec3 p0 = rootPos - right * hw + wd0;
        vec3 p1 = rootPos + right * hw + wd0;
        vec3 p2 = rootPos - right * hw + up * bH + wd1;
        vec3 p3 = rootPos + right * hw + up * bH + wd1;

        SetMeshOutputsEXT(4, 2);

        gl_MeshVerticesEXT[0].gl_Position = projection * view * vec4(p0, 1.0);
        outWorldPos[0] = p0; outNormal[0] = fn; outUV[0] = vec2(0.0, 0.0);
        outAlpha[0] = alpha; outVegType[0] = gVegType; outTexIndex[0] = gTexIndex;

        gl_MeshVerticesEXT[1].gl_Position = projection * view * vec4(p1, 1.0);
        outWorldPos[1] = p1; outNormal[1] = fn; outUV[1] = vec2(1.0, 0.0);
        outAlpha[1] = alpha; outVegType[1] = gVegType; outTexIndex[1] = gTexIndex;

        gl_MeshVerticesEXT[2].gl_Position = projection * view * vec4(p2, 1.0);
        outWorldPos[2] = p2; outNormal[2] = fn; outUV[2] = vec2(0.0, 1.0);
        outAlpha[2] = alpha; outVegType[2] = gVegType; outTexIndex[2] = gTexIndex;

        gl_MeshVerticesEXT[3].gl_Position = projection * view * vec4(p3, 1.0);
        outWorldPos[3] = p3; outNormal[3] = fn; outUV[3] = vec2(1.0, 1.0);
        outAlpha[3] = alpha; outVegType[3] = gVegType; outTexIndex[3] = gTexIndex;

        gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
        gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1, 3, 2);

    } else if (vt == 1u) {
        // Billboard: cross-billboard (two perpendicular quads forming X)
        float hw = bW;
        vec3 nA = normalize(vec3(sr, 0.0, cr));
        vec3 nB = normalize(vec3(cr, 0.0, -sr));

        SetMeshOutputsEXT(8, 4);

        writeVertex(0, rootPos, vec3(-hw, 0.0, 0.0), 0.0, bW*2.0, cr, sr, wp, alpha, nA);
        writeVertex(1, rootPos, vec3( hw, 0.0, 0.0), 0.0, bW*2.0, cr, sr, wp, alpha, nA);
        writeVertex(2, rootPos, vec3(-hw, bH,   0.0), 1.0, bW*2.0, cr, sr, wp, alpha, nA);
        writeVertex(3, rootPos, vec3( hw, bH,   0.0), 1.0, bW*2.0, cr, sr, wp, alpha, nA);

        float cr90 = -sr;
        float sr90 = cr;
        writeVertex(4, rootPos, vec3(-hw, 0.0, 0.0), 0.0, bW*2.0, cr90, sr90, wp, alpha, nB);
        writeVertex(5, rootPos, vec3( hw, 0.0, 0.0), 0.0, bW*2.0, cr90, sr90, wp, alpha, nB);
        writeVertex(6, rootPos, vec3(-hw, bH,   0.0), 1.0, bW*2.0, cr90, sr90, wp, alpha, nB);
        writeVertex(7, rootPos, vec3( hw, bH,   0.0), 1.0, bW*2.0, cr90, sr90, wp, alpha, nB);

        gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
        gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1, 3, 2);
        gl_PrimitiveTriangleIndicesEXT[2] = uvec3(4, 5, 6);
        gl_PrimitiveTriangleIndicesEXT[3] = uvec3(5, 7, 6);

    } else {
        // Grass (type 0 or any unknown): tapered blade (7 verts, 5 tris)
        float halfW = bW * 0.5;
        float midW = halfW * 0.5;
        vec3 bn = normalize(vec3(sr, 0.0, cr));

        SetMeshOutputsEXT(7, 5);

        writeVertex(0, rootPos, vec3(-halfW, 0.0, 0.0), 0.0, bW, cr, sr, wp, alpha, bn);
        writeVertex(1, rootPos, vec3( halfW, 0.0, 0.0), 0.0, bW, cr, sr, wp, alpha, bn);
        writeVertex(2, rootPos, vec3(-midW, bH*0.33, 0.0), 0.33, bW, cr, sr, wp, alpha, bn);
        writeVertex(3, rootPos, vec3( midW, bH*0.33, 0.0), 0.33, bW, cr, sr, wp, alpha, bn);
        writeVertex(4, rootPos, vec3(-midW*0.5, bH*0.66, 0.0), 0.66, bW, cr, sr, wp, alpha, bn);
        writeVertex(5, rootPos, vec3( midW*0.5, bH*0.66, 0.0), 0.66, bW, cr, sr, wp, alpha, bn);
        writeVertex(6, rootPos, vec3(0.0, bH, 0.0), 1.0, bW, cr, sr, wp, alpha, bn);

        gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
        gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1, 3, 2);
        gl_PrimitiveTriangleIndicesEXT[2] = uvec3(2, 3, 4);
        gl_PrimitiveTriangleIndicesEXT[3] = uvec3(3, 5, 4);
        gl_PrimitiveTriangleIndicesEXT[4] = uvec3(4, 5, 6);
    }
}
