#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;

// Local flag constants (must not conflict with #define in gpu_types.glsl)
const uint LOCAL_FLAG_NO_CULL       = 1u << 6;
const uint LOCAL_FLAG_NO_OCCLUDE    = 1u << 7;
// Note: FLAG_UNIFORM_SCALE is already defined in gpu_types.glsl as a macro

layout(std430, set = 0, binding = 0) readonly buffer ObjectBuffer {
    GPUObjectData objects[];
};

layout(set = 0, binding = 1) uniform CameraUBO {
    GPUCameraData camera;
};

uint getCommandsPerSection() {
    return camera.commandsPerBatch / camera.shaderGroupCount;
}

uint getSectionIndex(uint batch, uint shaderGroup) {
    return batch * camera.shaderGroupCount + shaderGroup;
}

layout(std430, set = 0, binding = 2) writeonly buffer DrawCommandBuffer {
    MeshTasksCommand drawCommands[];
};

layout(std430, set = 0, binding = 3) writeonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

layout(std430, set = 0, binding = 4) buffer DrawCountBuffer {
    BatchDrawStats batchStats[];
};

layout(set = 0, binding = 5) uniform sampler2D hiZTexture;

// Object to draw index mapping for cluster DAG rendering (VK-293)
// Each DAG object stores its drawIndex here, indexed by objectIndex
layout(std430, set = 0, binding = 6) writeonly buffer ObjectDrawIndexMap {
    uint objectDrawIndexMap[];
};

// VK-300: getMeshletLODData(), selectLOD(), findBestAvailableLOD() removed
// All objects now use DAG cluster rendering

vec4 transformBoundingSphere(vec4 localSphere, mat4 modelMatrix) {
    vec3 worldCenter = (modelMatrix * vec4(localSphere.xyz, 1.0)).xyz;
    float scaleX = length(modelMatrix[0].xyz);
    float scaleY = length(modelMatrix[1].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float maxScale = max(max(scaleX, scaleY), scaleZ);
    float worldRadius = localSphere.w * maxScale;
    return vec4(worldCenter, worldRadius);
}

bool sphereInFrustum(vec4 sphere, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        float distance = dot(frustumPlanes[i].xyz, sphere.xyz) + frustumPlanes[i].w;
        if (distance < -sphere.w) {
            return false;
        }
    }
    return true;
}

float projectSphereToScreen(vec4 worldSphere, mat4 projection, vec2 screenSize) {
    vec4 clipPos = projection * vec4(worldSphere.xyz, 1.0);
    if (clipPos.w <= 0.01) {
        return 10000.0;
    }
    float projectedRadius = worldSphere.w * abs(projection[1][1]) / clipPos.w;
    float screenDiameter = projectedRadius * screenSize.y;
    return screenDiameter;
}

// VK-300: selectLOD() and findBestAvailableLOD() removed - discrete LOD no longer used

bool hiZOcclusionTest(vec4 worldSphere, mat4 viewProjection, vec2 screenSize, uint hiZMipLevels) {
    vec3 center = worldSphere.xyz;
    float radius = worldSphere.w;
    vec3 aabbMin = center - vec3(radius);
    vec3 aabbMax = center + vec3(radius);

    vec4 corners[8];
    corners[0] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0);
    corners[1] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0);
    corners[2] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0);
    corners[3] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0);
    corners[4] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0);
    corners[5] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0);
    corners[6] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0);
    corners[7] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0);

    vec2 ndcMin = vec2(1.0);
    vec2 ndcMax = vec2(-1.0);
    float minDepth = 1.0;

    for (int i = 0; i < 8; i++) {
        if (corners[i].w <= 0.0) {
            return true;
        }
        vec3 ndc = corners[i].xyz / corners[i].w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        minDepth = min(minDepth, ndc.z);
    }

    ndcMin = clamp(ndcMin, vec2(-1.0), vec2(1.0));
    ndcMax = clamp(ndcMax, vec2(-1.0), vec2(1.0));

    if (minDepth < 0.0) {
        return true;
    }

    vec2 uvMin = ndcMin * 0.5 + 0.5;
    vec2 uvMax = ndcMax * 0.5 + 0.5;
    vec2 sizePixels = (uvMax - uvMin) * screenSize;
    float maxDimension = max(sizePixels.x, sizePixels.y);
    float mipLevel = ceil(log2(maxDimension));
    mipLevel = clamp(mipLevel, 0.0, float(hiZMipLevels - 1u));

    float hiZDepth = 0.0;
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, uvMin, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, uvMax, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, vec2(uvMin.x, uvMax.y), mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, vec2(uvMax.x, uvMin.y), mipLevel).r);

    return minDepth <= hiZDepth + 0.0001;
}

// VK-300: All objects now use DAG cluster rendering
// Discrete LOD path has been removed

void main() {
    uint objectIndex = gl_GlobalInvocationID.x;
    if (objectIndex >= camera.objectCount) {
        return;
    }

    GPUObjectData obj = objects[objectIndex];

    // All objects use DAG cluster rendering (VK-300)
    // Fill PerDrawData for material/transform info, store mapping for cluster traversal

    uint dagSection = 0;
    uint commandsPerSection = getCommandsPerSection();

    uint localDrawIndex = atomicAdd(batchStats[dagSection].drawCount, 1);
    if (localDrawIndex >= commandsPerSection) {
        atomicAdd(batchStats[dagSection].drawCount, uint(-1));
        objectDrawIndexMap[objectIndex] = 0xFFFFFFFFu;  // Invalid
        return;
    }

    uint globalDrawIndex = dagSection * commandsPerSection + localDrawIndex;

    // Store mapping from objectIndex to drawIndex for cluster traversal
    objectDrawIndexMap[objectIndex] = globalDrawIndex;

    // Fill PerDrawData with object transforms and materials
    perDrawData[globalDrawIndex].modelMatrix = obj.modelMatrix;

    mat3 modelMat3 = mat3(obj.modelMatrix);
    mat3 normalMat3;
    if ((obj.flags & FLAG_UNIFORM_SCALE) != 0u) {
        float scale = length(modelMat3[0]);
        normalMat3 = modelMat3 * (1.0 / scale);
    } else {
        normalMat3 = transpose(inverse(modelMat3));
    }
    perDrawData[globalDrawIndex].normalMatrix = mat4(normalMat3);

    perDrawData[globalDrawIndex].albedo = obj.albedo;
    perDrawData[globalDrawIndex].materialParams = obj.materialParams;
    perDrawData[globalDrawIndex].textureIndices0 = obj.textureIndices0;
    perDrawData[globalDrawIndex].textureIndices1 = obj.textureIndices1;
    perDrawData[globalDrawIndex].objectIndex = objectIndex;
    perDrawData[globalDrawIndex].flags = obj.flags;
    perDrawData[globalDrawIndex].iblDiffuse = obj.iblParams.x;
    perDrawData[globalDrawIndex].iblSpecular = obj.iblParams.y;
    perDrawData[globalDrawIndex].lodLevel = 0u;  // DAG handles LOD internally
    perDrawData[globalDrawIndex].shaderGroupIndex = obj.shaderGroupIndex;

    // Get meshlet info from DAG cluster info (lod0Data)
    // In DAG mode: lod0Data = {dagHeaderIdx, clusterOffset/meshletOffset, clusterCount/meshletCount, root}
    uint meshletOffset = obj.lod0Data.y;
    uint meshletCount = obj.lod0Data.z;
    perDrawData[globalDrawIndex].meshletOffset = meshletOffset;
    perDrawData[globalDrawIndex].meshletCount = meshletCount;
    perDrawData[globalDrawIndex].baseVertexOffset = 0u;
    perDrawData[globalDrawIndex].boneMatrixOffset = obj.boneMatrixOffset;
    perDrawData[globalDrawIndex].boneCount = 0u;
    perDrawData[globalDrawIndex].padding3 = 0u;

    // Generate draw command for fallback mode (when cluster data not available)
    // Objects WITH DAGFullyLoaded flag will be rendered via DAG traversal path,
    // so we skip generating fallback commands for them (set groupCountX = 0)
    // Objects WITHOUT DAGFullyLoaded flag use direct meshlet rendering
    MeshTasksCommand cmd;
    bool hasDAGData = (obj.flags & FLAG_DAG_FULLY_LOADED) != 0u;
    if (hasDAGData) {
        // Skip fallback rendering - this object will be rendered via DAG path
        cmd.groupCountX = 0u;
    } else {
        // Fallback: direct meshlet rendering
        // lod0Data.y/z contain meshlet offset/count for non-DAG objects
        cmd.groupCountX = (meshletCount + TASK_WORKGROUP_SIZE - 1u) / TASK_WORKGROUP_SIZE;
    }
    cmd.groupCountY = 1u;
    cmd.groupCountZ = 1u;
    drawCommands[globalDrawIndex] = cmd;
}
