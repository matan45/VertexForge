#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include "MeshletBufferTypes.hpp"

namespace render::gpudriven {

    // Streaming state for individual LOD levels
    enum class LODStreamState : uint8_t {
        NotRequested,   // LOD not yet requested for streaming
        Queued,         // In priority queue waiting to stream
        Streaming,      // Currently loading from disk
        Uploading,      // Data loaded, GPU transfer in progress
        Ready,          // Fully available for rendering
        Evicted         // Unloaded to make room (can reload)
    };
    
    constexpr uint32_t MAX_GPU_OBJECTS = 65536;
    
    constexpr uint32_t MAX_DRAW_COMMANDS = 700000;

  
    constexpr uint32_t DEFAULT_BATCH_COUNT = 4;
    constexpr uint32_t MAX_BATCH_COUNT = 8;
    
    constexpr uint32_t MAX_BINDLESS_TEXTURES = 4096;
    
    constexpr uint32_t MAX_SHADER_GROUPS = 16;
    
    constexpr uint32_t LOD_LEVEL_COUNT = 4;
    
    constexpr uint32_t CULL_WORKGROUP_SIZE = 64;
    
    constexpr uint32_t INVALID_TEXTURE_INDEX = 0xFFFFFFFF;
    
    constexpr float LOD_THRESHOLD_0 = 400.0f;  // LOD0 for >= 400 pixels
    constexpr float LOD_THRESHOLD_1 = 200.0f;  // LOD1 for >= 200 pixels
    constexpr float LOD_THRESHOLD_2 = 100.0f;  // LOD2 for >= 100 pixels
    // LOD3 for < 100 pixels
    
    struct alignas(16) LODDrawInfo {
        uint32_t vertexOffset;   // Offset into merged vertex buffer
        uint32_t indexOffset;    // Offset into merged index buffer (firstIndex)
        uint32_t indexCount;     // Number of indices for this LOD
        uint32_t vertexCount;    // Number of vertices for this LOD
    };
    
    struct alignas(16) GPUObjectData {
        glm::mat4 modelMatrix;           

        glm::vec4 boundingSphere;       

        // LOD data - geometry locations in merged buffer
        glm::uvec4 lod0Data;             
        glm::uvec4 lod1Data;             
        glm::uvec4 lod2Data;             
        glm::uvec4 lod3Data;             
        
        glm::vec4 lodThresholds;         

        // Material data
        glm::vec4 albedo;                
        glm::vec4 materialParams;        
        glm::vec4 iblParams;             

        // Bindless texture indices
        glm::uvec4 textureIndices0;      
        glm::uvec4 textureIndices1;      

        // Object flags and indices
        uint32_t flags;                 
        uint32_t entityId;              
        uint32_t availableLODMask;       
        uint32_t shaderGroupIndex;
        // Total: 256 bytes
    };
    static_assert(sizeof(GPUObjectData) == 256, "GPUObjectData must be 256 bytes");

    // Object flags (must match gpu_cull_lod.glsl)
    namespace ObjectFlags {
        constexpr uint32_t AlphaMask    = 1 << 4;   // Object uses alpha masking
        constexpr uint32_t NoCull       = 1 << 6;   // Never cull this object
        constexpr uint32_t NoOcclude    = 1 << 7;   // Object shouldn't occlude others
        constexpr uint32_t UniformScale = 1 << 9;   // Object has uniform scale (fast normal matrix path)
    }

  
    struct alignas(16) PerDrawData {
        glm::mat4 modelMatrix;           
        glm::mat4 normalMatrix;          

        glm::vec4 albedo;               
        glm::vec4 materialParams;        

        glm::uvec4 textureIndices0;     
        glm::uvec4 textureIndices1;      

        uint32_t objectIndex;            
        uint32_t flags;                
        float iblDiffuse;                
        float iblSpecular;             

        uint32_t lodLevel;               
        uint32_t shaderGroupIndex;       
        uint32_t padding1;               
        uint32_t padding2;              
        // Total: 224 bytes
    };
    static_assert(sizeof(PerDrawData) == 224, "PerDrawData must be 224 bytes to match GLSL");

    // Camera/view data for culling and rendering
    struct alignas(16) GPUCameraData {
        glm::mat4 view;                  // 64 bytes
        glm::mat4 projection;            // 64 bytes
        glm::mat4 viewProjection;        // 64 bytes
        glm::mat4 invViewProjection;     // 64 bytes

        glm::vec4 cameraPosition;        // 16 bytes - xyz = position, w = nearPlane
        glm::vec4 screenParams;          // 16 bytes - xy = resolution, zw = 1/resolution

        // Frustum planes for culling (Ax + By + Cz + D = 0)
        glm::vec4 frustumPlanes[6];      // 96 bytes

        float farPlane;                  // 4 bytes
        uint32_t objectCount;            // 4 bytes
        uint32_t hiZMipLevels;           // 4 bytes
        uint32_t frameIndex;             // 4 bytes

        uint32_t enableFrustumCulling;   // 4 bytes - boolean
        uint32_t enableOcclusionCulling; // 4 bytes - boolean
        uint32_t enableLODSelection;     // 4 bytes - boolean
        uint32_t batchCount;             // 4 bytes - number of batches for indirect rendering

        uint32_t commandsPerBatch;       // 4 bytes - max draw commands per batch
        uint32_t shaderGroupCount;       // 4 bytes - number of shader groups (for buffer indexing)
        uint32_t padding1;               // 4 bytes
        uint32_t padding2;               // 4 bytes
        // Total: 432 bytes
    };

    // Submesh location in merged buffer (CPU-side tracking)
    struct SubmeshLocation {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex;           // Index within the mesh

        std::array<LODDrawInfo, LOD_LEVEL_COUNT> lods;

        // Meshlet data per LOD level
        std::array<MeshletLODInfo, LOD_LEVEL_COUNT> meshletLods{};

        glm::vec3 aabbMin;
        glm::vec3 aabbMax;
        glm::vec4 boundingSphere;        // xyz = center, w = radius

        // Streaming state per LOD level
        std::array<LODStreamState, LOD_LEVEL_COUNT> lodStates{};

        // Calculate bounding sphere from AABB
        void calculateBoundingSphere() {
            glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
            float radius = glm::length(aabbMax - center);
            boundingSphere = glm::vec4(center, radius);
        }

       
        bool hasRenderableLOD() const {
            for (const auto& state : lodStates) {
                if (state == LODStreamState::Ready) return true;
            }
            return false;
        }

       
        // Returns LOD_LEVEL_COUNT if no LOD is ready
        uint32_t getBestAvailableLOD(uint32_t requestedLOD) const {
            // First try to find a LOD >= requested (lower detail is acceptable)
            for (uint32_t lod = requestedLOD; lod < LOD_LEVEL_COUNT; ++lod) {
                if (lodStates[lod] == LODStreamState::Ready) return lod;
            }
            // Fallback to any ready LOD (prefer lower index = higher detail)
            for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod) {
                if (lodStates[lod] == LODStreamState::Ready) return lod;
            }
            return LOD_LEVEL_COUNT; // None ready
        }

        // Get bitmask of available LODs (for GPU)
        uint32_t getAvailableLODMask() const {
            uint32_t mask = 0;
            for (uint32_t i = 0; i < LOD_LEVEL_COUNT; ++i) {
                if (lodStates[i] == LODStreamState::Ready) {
                    mask |= (1u << i);
                }
            }
            return mask;
        }

        // Check if meshlet data is available for this submesh
        bool hasMeshletData() const {
            for (const auto& mlod : meshletLods) {
                if (mlod.meshletCount > 0) return true;
            }
            return false;
        }

        // Get best available LOD with meshlet data
        uint32_t getBestAvailableMeshletLOD(uint32_t requestedLOD) const {
            // First try requested or lower detail
            for (uint32_t lod = requestedLOD; lod < LOD_LEVEL_COUNT; ++lod) {
                if (meshletLods[lod].meshletCount > 0 &&
                    lodStates[lod] == LODStreamState::Ready) {
                    return lod;
                }
            }
            // Fallback to any available meshlet LOD
            for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod) {
                if (meshletLods[lod].meshletCount > 0 &&
                    lodStates[lod] == LODStreamState::Ready) {
                    return lod;
                }
            }
            return LOD_LEVEL_COUNT; // None ready
        }
    };

    // Mesh registration info for merged buffer
    struct MergedMeshInfo {
        std::string meshPath;
        std::vector<SubmeshLocation> submeshes;
        uint32_t firstSubmeshIndex;      // First submesh index in global array
        uint32_t submeshCount;

        glm::vec3 aabbMin;               // Combined AABB of all submeshes
        glm::vec3 aabbMax;
    };

    // Draw statistics for debugging
    struct GPUDrivenStats {
        uint32_t totalObjects;           // Total objects submitted
        uint32_t visibleObjects;         // Objects passing culling
        uint32_t drawCalls;              // Actual draw calls issued (should be 1 for full GPU-driven)
        uint32_t trianglesTotal;         // Total triangles in scene
        uint32_t trianglesRendered;      // Triangles after culling/LOD

        // Per-LOD distribution
        uint32_t objectsLOD0;
        uint32_t objectsLOD1;
        uint32_t objectsLOD2;
        uint32_t objectsLOD3;

        // Culling statistics
        uint32_t culledByFrustum;
        uint32_t culledByOcclusion;
    };

    // VkDrawIndexedIndirectCommand structure (matches Vulkan spec)
    struct DrawIndexedIndirectCommand {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t  vertexOffset;
        uint32_t firstInstance;
    };
    static_assert(sizeof(DrawIndexedIndirectCommand) == 20, "DrawIndexedIndirectCommand must match VkDrawIndexedIndirectCommand");

    // Per-batch statistics for multi-batch indirect rendering
    // Must match BatchDrawStats in gpu_cull_lod.glsl
    struct alignas(32) BatchDrawStats {
        uint32_t drawCount;         
        uint32_t lodCount0;          
        uint32_t lodCount1;          
        uint32_t lodCount2;          
        uint32_t lodCount3;          
        uint32_t culledByFrustum;   
        uint32_t culledByOcclusion;  // Objects culled by Hi-Z occlusion
        uint32_t padding;            // Padding to 32 bytes
    };
    static_assert(sizeof(BatchDrawStats) == 32, "BatchDrawStats must be 32 bytes for GPU alignment");

}
