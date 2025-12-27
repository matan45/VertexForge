#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <cstdint>

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

    // Maximum supported objects in GPU-driven rendering
    constexpr uint32_t MAX_GPU_OBJECTS = 65536;

    // Maximum supported draw commands per batch
    // 700,000 commands * 4 batches = 2.8M objects capacity
    // Memory: 4 batches * 700K * (20 + 224) bytes = ~683 MB
    constexpr uint32_t MAX_DRAW_COMMANDS = 700000;

    // Batch configuration for multi-batch indirect rendering
    constexpr uint32_t DEFAULT_BATCH_COUNT = 4;
    constexpr uint32_t MAX_BATCH_COUNT = 8;

    // Maximum bindless textures
    constexpr uint32_t MAX_BINDLESS_TEXTURES = 4096;

    // LOD level count (matches existing system)
    constexpr uint32_t LOD_LEVEL_COUNT = 4;

    // Compute shader workgroup size
    constexpr uint32_t CULL_WORKGROUP_SIZE = 64;

    // Invalid texture index for bindless textures
    constexpr uint32_t INVALID_TEXTURE_INDEX = 0xFFFFFFFF;

    // LOD screen-space thresholds (in pixels) - matches existing CPU thresholds
    constexpr float LOD_THRESHOLD_0 = 400.0f;  // LOD0 for >= 400 pixels
    constexpr float LOD_THRESHOLD_1 = 200.0f;  // LOD1 for >= 200 pixels
    constexpr float LOD_THRESHOLD_2 = 100.0f;  // LOD2 for >= 100 pixels
    // LOD3 for < 100 pixels

    // Per-LOD draw info - where geometry lives in merged buffer
    struct alignas(16) LODDrawInfo {
        uint32_t vertexOffset;   // Offset into merged vertex buffer
        uint32_t indexOffset;    // Offset into merged index buffer (firstIndex)
        uint32_t indexCount;     // Number of indices for this LOD
        uint32_t vertexCount;    // Number of vertices for this LOD
    };

    // Per-object data uploaded to GPU for culling and rendering
    // Size: 256 bytes (well-aligned for GPU access)
    struct alignas(16) GPUObjectData {
        glm::mat4 modelMatrix;           // 64 bytes - World transform

        glm::vec4 boundingSphere;        // 16 bytes - xyz = center (local space), w = radius

        // LOD data - geometry locations in merged buffer
        glm::uvec4 lod0Data;             // 16 bytes - vertexOffset, indexOffset, indexCount, vertexCount
        glm::uvec4 lod1Data;             // 16 bytes
        glm::uvec4 lod2Data;             // 16 bytes
        glm::uvec4 lod3Data;             // 16 bytes

        // LOD thresholds (can be customized per-object)
        glm::vec4 lodThresholds;         // 16 bytes - threshold0, threshold1, threshold2, lodBias

        // Material data
        glm::vec4 albedo;                // 16 bytes - RGB + alpha
        glm::vec4 materialParams;        // 16 bytes - metallic, roughness, ao, emission
        glm::vec4 iblParams;             // 16 bytes - iblDiffuse, iblSpecular, padding, padding

        // Bindless texture indices
        glm::uvec4 textureIndices0;      // 16 bytes - albedo, normal, orm, metallic
        glm::uvec4 textureIndices1;      // 16 bytes - roughness, ao, emission, height

        // Object flags and indices
        uint32_t flags;                  // 4 bytes - visibility flags, blend mode, etc.
        uint32_t entityId;               // 4 bytes - for picking/selection
        uint32_t availableLODMask;       // 4 bytes - bits 0-3: which LODs are ready for streaming
        uint32_t padding1;               // 4 bytes
        // Total: 256 bytes

        // Helper to set LOD data from LODDrawInfo
        void setLODData(uint32_t level, const LODDrawInfo& info) {
            glm::uvec4 data(info.vertexOffset, info.indexOffset, info.indexCount, info.vertexCount);
            switch (level) {
                case 0: lod0Data = data; break;
                case 1: lod1Data = data; break;
                case 2: lod2Data = data; break;
                case 3: lod3Data = data; break;
            }
        }

        // Helper to get LOD data
        glm::uvec4 getLODData(uint32_t level) const {
            switch (level) {
                case 0: return lod0Data;
                case 1: return lod1Data;
                case 2: return lod2Data;
                default: return lod3Data;
            }
        }
    };
    static_assert(sizeof(GPUObjectData) == 256, "GPUObjectData must be 256 bytes");

    // Object flags
    namespace ObjectFlags {
        constexpr uint32_t Visible       = 1 << 0;   // Object is currently visible
        constexpr uint32_t CastShadow    = 1 << 1;   // Object casts shadows
        constexpr uint32_t ReceiveShadow = 1 << 2;   // Object receives shadows
        constexpr uint32_t Transparent   = 1 << 3;   // Object uses alpha blending
        constexpr uint32_t AlphaMask     = 1 << 4;   // Object uses alpha masking
        constexpr uint32_t DoubleSided   = 1 << 5;   // Object is double-sided
        constexpr uint32_t NoCull        = 1 << 6;   // Never cull this object
        constexpr uint32_t NoOcclude     = 1 << 7;   // Object shouldn't occlude others
        constexpr uint32_t Selected      = 1 << 8;   // Object is selected (editor)
    }

    // Per-draw data output by culling shader, consumed by vertex/fragment shaders
    // This is written alongside VkDrawIndexedIndirectCommand
    struct alignas(16) PerDrawData {
        glm::mat4 modelMatrix;           // 64 bytes
        glm::mat4 normalMatrix;          // 64 bytes - pre-computed transpose(inverse(mat3(model)))

        glm::vec4 albedo;                // 16 bytes
        glm::vec4 materialParams;        // 16 bytes - metallic, roughness, ao, emission

        glm::uvec4 textureIndices0;      // 16 bytes
        glm::uvec4 textureIndices1;      // 16 bytes

        uint32_t objectIndex;            // 4 bytes - Index into GPUObjectData array
        uint32_t flags;                  // 4 bytes
        float iblDiffuse;                // 4 bytes
        float iblSpecular;               // 4 bytes

        uint32_t lodLevel;               // 4 bytes - Selected LOD level (for debug visualization)
        uint32_t padding0;               // 4 bytes
        uint32_t padding1;               // 4 bytes
        uint32_t padding2;               // 4 bytes
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
        uint32_t padding0;               // 4 bytes
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

        // Check if any LOD has valid geometry
        bool hasValidGeometry() const {
            for (const auto& lod : lods) {
                if (lod.indexCount > 0) return true;
            }
            return false;
        }

        // Check if any LOD is ready for rendering
        bool hasRenderableLOD() const {
            for (const auto& state : lodStates) {
                if (state == LODStreamState::Ready) return true;
            }
            return false;
        }

        // Get the best available LOD that is ready for rendering
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

        // Mark all LODs as ready (for non-streaming loads)
        void markAllLODsReady() {
            for (auto& state : lodStates) {
                state = LODStreamState::Ready;
            }
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
        uint32_t drawCount;          // Number of visible objects in this batch
        uint32_t lodCount0;          // Objects using LOD0
        uint32_t lodCount1;          // Objects using LOD1
        uint32_t lodCount2;          // Objects using LOD2
        uint32_t lodCount3;          // Objects using LOD3
        uint32_t culledByFrustum;    // Objects culled by frustum
        uint32_t culledByOcclusion;  // Objects culled by Hi-Z occlusion
        uint32_t padding;            // Padding to 32 bytes
    };
    static_assert(sizeof(BatchDrawStats) == 32, "BatchDrawStats must be 32 bytes for GPU alignment");

}
