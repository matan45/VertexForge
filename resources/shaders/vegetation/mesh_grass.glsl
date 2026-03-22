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

vec3 calcWind(vec3 worldPos, float vertH, float windPhase) {
    float windSpeed = windDirectionAndSpeed.w;
    vec3 windDir = windDirectionAndSpeed.xyz;
    float t = windGustParams.w + windPhase;
    float gustStr = windGustParams.x;
    float gustFreq = windGustParams.y;

    float phase = dot(worldPos.xz, windDir.xz) * 0.5 + t * windSpeed;
    float baseSway = sin(phase) * 0.5 + 0.5;
    float gust = sin(t * gustFreq * 6.28318 + windHash(worldPos.xz * 0.01) * 6.28318) * 0.5 + 0.5;
    float strength = (baseSway + gust * gustStr) * windSpeed;
    float hFactor = vertH * vertH;

    vec3 disp = windDir * strength * hFactor;
    vec3 perpDir = vec3(-windDir.z, 0.0, windDir.x);
    disp += perpDir * sin(phase * 1.3 + 0.7) * 0.3 * hFactor * windSpeed;
    return disp;
}

void emitVert(uint i, vec3 wp, vec3 n, vec2 uv, float a, uint vt, uint ti) {
    gl_MeshVerticesEXT[i].gl_Position = projection * view * vec4(wp, 1.0);
    outWorldPos[i] = wp;
    outNormal[i] = n;
    outUV[i] = uv;
    outAlpha[i] = a;
    outVegType[i] = vt;
    outTexIndex[i] = ti;
}

void main() {
    uint instanceIdx = payload.instanceIndices[gl_WorkGroupID.x];
    float dist = payload.distanceToCamera[gl_WorkGroupID.x];

    uint base = instanceIdx * 3u;
    vec4 posAndRot = grassInstances[base];
    vec4 scaleAndDensity = grassInstances[base + 1u];
    vec4 colorTint = grassInstances[base + 2u];

    vec3 rootPos = posAndRot.xyz;
    float rot = posAndRot.w;
    float bH = scaleAndDensity.x;
    float bW = scaleAndDensity.y;
    float wp = scaleAndDensity.w;
    uint vt = uint(colorTint.w + 0.5); // round to nearest uint
    uint ti = uint(colorTint.x + 0.5); // texture index stored as float
    uint bbMode = uint(colorTint.y + 0.5);

    float alpha = 1.0;
    if (dist > fadeStartDistance) {
        alpha = clamp(1.0 - (dist - fadeStartDistance) / (fadeEndDistance - fadeStartDistance), 0.0, 1.0);
    }

    float cr = cos(rot);
    float sr = sin(rot);

    // Billboard - use height as both height and width for square-ish billboard
    float bbSize = bH * 0.5;

    if (bbMode == 1u) {
        // Camera-facing quad
        vec3 toCamera = normalize(cameraPos - rootPos);
        vec3 up = vec3(0.0, 1.0, 0.0);
        vec3 right = normalize(cross(up, toCamera));
        float hw = bbSize;

        vec3 w0 = calcWind(rootPos, 0.0, wp);
        vec3 w1 = calcWind(rootPos, 1.0, wp);

        SetMeshOutputsEXT(4, 2);
        emitVert(0, rootPos - right*hw + w0,          toCamera, vec2(0,0), alpha, vt, ti);
        emitVert(1, rootPos + right*hw + w0,          toCamera, vec2(1,0), alpha, vt, ti);
        emitVert(2, rootPos - right*hw + up*bH + w1,  toCamera, vec2(0,1), alpha, vt, ti);
        emitVert(3, rootPos + right*hw + up*bH + w1,  toCamera, vec2(1,1), alpha, vt, ti);
        gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0,1,2);
        gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1,3,2);
    } else {
        // Cross-billboard (X shape)
        float hw = bbSize;
        vec3 nA = normalize(vec3(sr, 0.0, cr));
        vec3 nB = normalize(vec3(cr, 0.0, -sr));

        vec3 w0 = calcWind(rootPos, 0.0, wp);
        vec3 w1 = calcWind(rootPos, 1.0, wp);

        vec3 a0 = vec3(-hw*cr, 0.0, -hw*sr);
        vec3 a1 = vec3( hw*cr, 0.0,  hw*sr);
        vec3 a2 = vec3(-hw*cr, bH, -hw*sr);
        vec3 a3 = vec3( hw*cr, bH,  hw*sr);

        float cr90 = -sr, sr90 = cr;
        vec3 b0 = vec3(-hw*cr90, 0.0, -hw*sr90);
        vec3 b1 = vec3( hw*cr90, 0.0,  hw*sr90);
        vec3 b2 = vec3(-hw*cr90, bH, -hw*sr90);
        vec3 b3 = vec3( hw*cr90, bH,  hw*sr90);

        SetMeshOutputsEXT(8, 4);
        emitVert(0, rootPos+a0+w0, nA, vec2(0,0), alpha, vt, ti);
        emitVert(1, rootPos+a1+w0, nA, vec2(1,0), alpha, vt, ti);
        emitVert(2, rootPos+a2+w1, nA, vec2(0,1), alpha, vt, ti);
        emitVert(3, rootPos+a3+w1, nA, vec2(1,1), alpha, vt, ti);
        emitVert(4, rootPos+b0+w0, nB, vec2(0,0), alpha, vt, ti);
        emitVert(5, rootPos+b1+w0, nB, vec2(1,0), alpha, vt, ti);
        emitVert(6, rootPos+b2+w1, nB, vec2(0,1), alpha, vt, ti);
        emitVert(7, rootPos+b3+w1, nB, vec2(1,1), alpha, vt, ti);
        gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0,1,2);
        gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1,3,2);
        gl_PrimitiveTriangleIndicesEXT[2] = uvec3(4,5,6);
        gl_PrimitiveTriangleIndicesEXT[3] = uvec3(5,7,6);
    }
}
