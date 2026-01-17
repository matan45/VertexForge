#type COMPUTE
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;

const uint FLAG_NO_CULL       = 1u << 6;
const uint FLAG_NO_OCCLUDE    = 1u << 7;
const uint FLAG_UNIFORM_SCALE = 1u << 9;

// Must match GPUObjectData in GPUDrivenTypes.hpp (320 bytes)
struct GPUObjectData {
    mat4 modelMatrix;          

    vec4 boundingSphere;       

    uvec4 lod0Data;             
    uvec4 lod1Data;             
    uvec4 lod2Data;             
    uvec4 lod3Data;            

    vec4 lodThresholds;         

    vec4 albedo;                
    vec4 materialParams;        
    vec4 iblParams;             

    uvec4 textureIndices0;      
    uvec4 textureIndices1;     

    uint flags;                
    uint entityId;              
    uint availableLODMask;      
    uint shaderGroupIndex;      

    // Meshlet LOD data - meshlet locations in meshlet buffer
    // Each uvec4: (meshletOffset, meshletCount, baseVertexOffset, padding/boneMatrixOffset)
    // Note: meshletLod3.w stores boneMatrixOffset (0xFFFFFFFF = static mesh)
    uvec4 meshletLod0;
    uvec4 meshletLod1;
    uvec4 meshletLod2;
    uvec4 meshletLod3;
};

// Must match PerDrawData in GPUDrivenTypes.hpp (240 bytes)
struct PerDrawData {
    mat4 modelMatrix;           
    mat4 normalMatrix;          

    vec4 albedo;                
    vec4 materialParams;        

    uvec4 textureIndices0;      
    uvec4 textureIndices1;      

    uint objectIndex;          
    uint flags;                 
    float iblDiffuse;           
    float iblSpecular;         

    uint lodLevel;              
    uint shaderGroupIndex;      
    // Meshlet dispatch info (for mesh shader path)
    uint meshletOffset;        
    uint meshletCount;          

    uint baseVertexOffset;
    uint boneMatrixOffset;      // Offset into global bone SSBO, 0xFFFFFFFF if static
    uint boneCount;             // Number of bones for this object
    uint padding3;              
};

// VkDrawMeshTasksIndirectCommandEXT (12 bytes)
struct MeshTasksCommand {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};

// Must match GPUCameraData in GPUDrivenTypes.hpp
struct CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 invViewProjection;

    vec4 cameraPosition;       
    vec4 screenParams;         

    vec4 frustumPlanes[6];

    float farPlane;
    uint objectCount;
    uint hiZMipLevels;
    uint frameIndex;

    uint enableFrustumCulling;
    uint enableOcclusionCulling;
    uint enableLODSelection;
    uint batchCount;            

    uint commandsPerBatch;     
    uint shaderGroupCount;     
    uint padding1;
    uint padding2;
};

layout(std430, set = 0, binding = 0) readonly buffer ObjectBuffer {
    GPUObjectData objects[];
};

layout(set = 0, binding = 1) uniform CameraUBO {
    CameraData camera;
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

// Must match BatchDrawStats in GPUDrivenTypes.hpp
struct BatchDrawStats {
    uint drawCount;         
    uint lodCount0;         
    uint lodCount1;          
    uint lodCount2;          
    uint lodCount3;          
    uint culledByFrustum;    
    uint culledByOcclusion;  
    uint padding;            
};

layout(std430, set = 0, binding = 4) buffer DrawCountBuffer {
    BatchDrawStats batchStats[];
};

layout(set = 0, binding = 5) uniform sampler2D hiZTexture;

uvec4 getMeshletLODData(GPUObjectData obj, uint level) {
    switch (level) {
        case 0: return obj.meshletLod0;
        case 1: return obj.meshletLod1;
        case 2: return obj.meshletLod2;
        default: return obj.meshletLod3;
    }
}

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

uint selectLOD(float screenPixels, vec4 thresholds) {
    float adjustedPixels = screenPixels * pow(2.0, -thresholds.w);
    if (adjustedPixels > thresholds.x) return 0;
    if (adjustedPixels > thresholds.y) return 1;
    if (adjustedPixels > thresholds.z) return 2;
    return 3;
}

uint findBestAvailableLOD(uint targetLOD, uint availableMask) {
    if (availableMask == 0xFu) {
        return targetLOD;
    }
    if (availableMask == 0u) {
        return 0xFFFFFFFFu;
    }
    for (uint lod = targetLOD; lod < 4u; ++lod) {
        if ((availableMask & (1u << lod)) != 0u) {
            return lod;
        }
    }
    for (uint lod = 0u; lod < 4u; ++lod) {
        if ((availableMask & (1u << lod)) != 0u) {
            return lod;
        }
    }
    return 0xFFFFFFFFu;
}

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

void main() {
    uint objectIndex = gl_GlobalInvocationID.x;
    if (objectIndex >= camera.objectCount) {
        return;
    }

    GPUObjectData obj = objects[objectIndex];
    vec4 worldSphere = transformBoundingSphere(obj.boundingSphere, obj.modelMatrix);

    uint batchIndex = objectIndex % camera.batchCount;
    uint shaderGroup = obj.shaderGroupIndex;
    uint sectionIndex = getSectionIndex(batchIndex, shaderGroup);
    uint commandsPerSection = getCommandsPerSection();

    // Frustum Culling
    if (camera.enableFrustumCulling != 0u && (obj.flags & FLAG_NO_CULL) == 0u) {
        if (!sphereInFrustum(worldSphere, camera.frustumPlanes)) {
            atomicAdd(batchStats[sectionIndex].culledByFrustum, 1);
            return;
        }
    }

    // Hi-Z Occlusion Culling
    if (camera.enableOcclusionCulling != 0u && (obj.flags & FLAG_NO_OCCLUDE) == 0u) {
        if (camera.hiZMipLevels > 0u) {
            if (!hiZOcclusionTest(worldSphere, camera.viewProjection, camera.screenParams.xy, camera.hiZMipLevels)) {
                atomicAdd(batchStats[sectionIndex].culledByOcclusion, 1);
                return;
            }
        }
    }

    // LOD Selection
    uint targetLOD = 0;
    if (camera.enableLODSelection != 0u) {
        vec4 viewSphere = camera.view * vec4(worldSphere.xyz, 1.0);
        viewSphere.w = worldSphere.w;
        float screenPixels = projectSphereToScreen(viewSphere, camera.projection, camera.screenParams.xy);
        targetLOD = selectLOD(screenPixels, obj.lodThresholds);
    }

    uint lodLevel = findBestAvailableLOD(targetLOD, obj.availableLODMask);
    if (lodLevel == 0xFFFFFFFFu) {
        return;
    }

    uvec4 meshletLodData = getMeshletLODData(obj, lodLevel);
    uint meshletOffset = meshletLodData.x;
    uint meshletCount = meshletLodData.y;
    uint baseVertexOffset = meshletLodData.z;

    while (lodLevel > 0u && meshletCount == 0u) {
        lodLevel--;
        if ((obj.availableLODMask & (1u << lodLevel)) == 0u) {
            continue;
        }
        meshletLodData = getMeshletLODData(obj, lodLevel);
        meshletOffset = meshletLodData.x;
        meshletCount = meshletLodData.y;
        baseVertexOffset = meshletLodData.z;
    }

    if (meshletCount == 0u) {
        return;
    }

    // Emit Mesh Shader Dispatch Command
    uint localDrawIndex = atomicAdd(batchStats[sectionIndex].drawCount, 1);
    if (localDrawIndex >= commandsPerSection) {
        atomicAdd(batchStats[sectionIndex].drawCount, uint(-1));
        return;
    }

    switch (lodLevel) {
        case 0u: atomicAdd(batchStats[sectionIndex].lodCount0, 1); break;
        case 1u: atomicAdd(batchStats[sectionIndex].lodCount1, 1); break;
        case 2u: atomicAdd(batchStats[sectionIndex].lodCount2, 1); break;
        default: atomicAdd(batchStats[sectionIndex].lodCount3, 1); break;
    }

    uint globalDrawIndex = sectionIndex * commandsPerSection + localDrawIndex;
    uint taskGroupCount = (meshletCount + TASK_WORKGROUP_SIZE - 1u) / TASK_WORKGROUP_SIZE;

    drawCommands[globalDrawIndex].groupCountX = taskGroupCount;
    drawCommands[globalDrawIndex].groupCountY = 1u;
    drawCommands[globalDrawIndex].groupCountZ = 1u;

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
    perDrawData[globalDrawIndex].lodLevel = lodLevel;
    perDrawData[globalDrawIndex].shaderGroupIndex = obj.shaderGroupIndex;
    perDrawData[globalDrawIndex].meshletOffset = meshletOffset;
    perDrawData[globalDrawIndex].meshletCount = meshletCount;
    perDrawData[globalDrawIndex].baseVertexOffset = baseVertexOffset;
    perDrawData[globalDrawIndex].boneMatrixOffset = obj.meshletLod3.w;  // 0xFFFFFFFF for static meshes
    perDrawData[globalDrawIndex].boneCount = 0u;  // Not used currently, bone count determined per-vertex
    perDrawData[globalDrawIndex].padding3 = 0u;
}
