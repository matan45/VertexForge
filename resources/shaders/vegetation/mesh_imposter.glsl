#type MESH
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 32) in;
layout(triangles, max_vertices = 4, max_primitives = 2) out;

struct TreeInstance {
    mat4 modelMatrix;
    vec4 boundingSphere;
    uint speciesId;
    uint lodMask;
    float lodDistances[3];
    uint padding;
};

layout(std430, set = 0, binding = 2) readonly buffer TreeInstanceBuffer {
    TreeInstance allInstances[];
};

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
};

struct ImposterConfig {
    uint atlasTextureIndex;
    uint horizontalAngles;
    uint verticalAngles;
    uint viewResolution;
    float atlasWidth;
    float atlasHeight;
    float treeHeight;
    float treeWidth;
};

layout(std430, set = 3, binding = 0) readonly buffer ImposterConfigBuffer {
    ImposterConfig imposterConfigs[];
};

struct ImposterPayload {
    uint instanceIndices[32];
};

taskPayloadSharedEXT ImposterPayload payload;

layout(location = 0) out vec2 outUV[];
layout(location = 1) out flat uint outAtlasIndex[];

void main() {
    uint tid = gl_LocalInvocationID.x;

    uint instanceIdx = payload.instanceIndices[tid];
    TreeInstance inst = allInstances[instanceIdx];

    ImposterConfig config = imposterConfigs[inst.speciesId];

    vec3 center = vec3(inst.modelMatrix[3]);
    float scale = length(vec3(inst.modelMatrix[0]));

    float halfW = config.treeWidth * scale * 0.5;
    float fullH = config.treeHeight * scale;

    // Cylindrical billboard - rotate around Y to face camera
    vec3 toCamera = cameraPos - center;
    toCamera.y = 0.0;
    toCamera = normalize(toCamera);
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), toCamera));

    // Select atlas frame based on view angle
    float angle = atan(toCamera.z, toCamera.x);
    if (angle < 0.0) angle += 6.28318;

    uint totalViews = config.horizontalAngles * config.verticalAngles;
    uint hIdx = uint(angle / 6.28318 * float(config.horizontalAngles)) % config.horizontalAngles;
    uint viewIdx = hIdx; // Ground-level view

    uint cols = uint(ceil(sqrt(float(totalViews))));
    uint col = viewIdx % cols;
    uint row = viewIdx / cols;

    float uSize = float(config.viewResolution) / config.atlasWidth;
    float vSize = float(config.viewResolution) / config.atlasHeight;
    float uOffset = float(col) * uSize;
    float vOffset = float(row) * vSize;

    vec3 positions[4];
    positions[0] = center - right * halfW;
    positions[1] = center + right * halfW;
    positions[2] = center - right * halfW + vec3(0, fullH, 0);
    positions[3] = center + right * halfW + vec3(0, fullH, 0);

    vec2 uvs[4];
    uvs[0] = vec2(uOffset, vOffset + vSize);
    uvs[1] = vec2(uOffset + uSize, vOffset + vSize);
    uvs[2] = vec2(uOffset, vOffset);
    uvs[3] = vec2(uOffset + uSize, vOffset);

    SetMeshOutputsEXT(4, 2);

    for (uint v = 0; v < 4; v++) {
        gl_MeshVerticesEXT[v].gl_Position = projection * view * vec4(positions[v], 1.0);
        outUV[v] = uvs[v];
        outAtlasIndex[v] = config.atlasTextureIndex;
    }

    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
    gl_PrimitiveTriangleIndicesEXT[1] = uvec3(1, 3, 2);
}
