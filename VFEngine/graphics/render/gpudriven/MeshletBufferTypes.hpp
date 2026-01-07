#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

namespace render::gpudriven {

    // Maximum buffer capacities for meshlet rendering
    constexpr uint32_t MAX_MESHLET_COUNT = 4 * 1024 * 1024;         
    constexpr uint32_t MAX_MESHLET_VERTEX_INDICES = 32 * 1024 * 1024; 
    constexpr uint32_t MAX_MESHLET_PRIMITIVES = 32 * 1024 * 1024;    

    // Meshlet sizes (must match resource::MAX_MESHLET_VERTICES/PRIMITIVES)
    constexpr uint32_t MESHLET_MAX_VERTICES = 64;
    constexpr uint32_t MESHLET_MAX_PRIMITIVES = 124;

    // GPU-side meshlet structure (must match shader layout)
    struct alignas(16) GPUMeshlet {
        uint32_t vertexOffset;       
        uint32_t primitiveOffset;    
        uint8_t  vertexCount;       
        uint8_t  primitiveCount;     
        uint16_t padding0;
        
        uint32_t globalVertexOffset;
        
        glm::vec4 boundingSphere;
        
        glm::vec4 cone;
    };
    static_assert(sizeof(GPUMeshlet) == 48, "GPUMeshlet must be 48 bytes");

    // Per-LOD meshlet info (fits in uvec4 for GPU upload)
    struct alignas(16) MeshletLODInfo {
        uint32_t meshletOffset;      // First meshlet index in global meshlet buffer
        uint32_t meshletCount;
        uint32_t baseVertexOffset;
        uint32_t padding;
    };
    static_assert(sizeof(MeshletLODInfo) == 16, "MeshletLODInfo must be 16 bytes");

}
