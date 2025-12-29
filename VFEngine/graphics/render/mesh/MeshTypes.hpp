#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include "math/Frustum.hpp"
#include "resource/Types.hpp"
#include "material/MaterialTypes.hpp"
#include <array>
#include <string>
#include <unordered_map>

namespace material
{
    enum class BlendMode : uint8_t;
}

namespace render::mesh
{
    // Texture index packing utilities
    // Pack 4 texture indices (8 bits each, range 0-254, 255 = no texture) into a single uint32
    constexpr uint8_t TEXTURE_INDEX_NONE = 255;

    inline uint32_t packTextureIndices(uint8_t idx0, uint8_t idx1, uint8_t idx2, uint8_t idx3) {
        return static_cast<uint32_t>(idx0) |
               (static_cast<uint32_t>(idx1) << 8) |
               (static_cast<uint32_t>(idx2) << 16) |
               (static_cast<uint32_t>(idx3) << 24);
    }

    inline uint8_t unpackTextureIndex(uint32_t packed, int slot) {
        return static_cast<uint8_t>((packed >> (slot * 8)) & 0xFF);
    }
    
    inline uint8_t floatToPackedIndex(float idx) {
        return (idx < 0.0f) ? TEXTURE_INDEX_NONE : static_cast<uint8_t>(idx);
    }
    
    inline float packedToFloatIndex(uint8_t idx) {
        return (idx == TEXTURE_INDEX_NONE) ? -1.0f : static_cast<float>(idx);
    }
}

namespace render::mesh
{
    // Forward declare blend mode for use in render data
    using BlendMode = ::material::BlendMode;

    // GPU buffers for a single LOD level (used by preview windows)
    struct LODGPUBuffers
    {
        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexBufferMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexBufferMemory;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;

        bool isValid() const { return indexCount > 0 || vertexCount > 0; }
    };

    
    struct SubMeshGPUData
    {
        std::string name; // Submesh name for material assignment
        std::array<LODGPUBuffers, resource::LOD_LEVEL_COUNT> lodLevels; // 4 LOD levels
        math::AABB boundingBox; // AABB for this submesh (local space, computed from LOD0)

        const LODGPUBuffers& getLOD(uint32_t level) const
        {
            return lodLevels[std::min(level, resource::LOD_LEVEL_COUNT - 1)];
        }
    };


    // Mesh with GPU buffers (used by preview windows)
    struct MeshGPUData
    {
        std::vector<SubMeshGPUData> subMeshes;
        math::AABB boundingBox; // Combined AABB of all submeshes (local space)

        int findSubmeshIndex(const std::string& name) const
        {
            for (size_t i = 0; i < subMeshes.size(); ++i)
            {
                if (subMeshes[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }
    };
    
    struct LODMetadata
    {
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;

        bool isValid() const { return vertexCount > 0 || indexCount > 0; }
    };
    
    struct SubMeshMetadata
    {
        std::string name;
        std::array<LODMetadata, resource::LOD_LEVEL_COUNT> lodLevels;
        math::AABB boundingBox;

        const LODMetadata& getLOD(uint32_t level) const
        {
            return lodLevels[std::min(level, resource::LOD_LEVEL_COUNT - 1)];
        }

        uint32_t getTotalVertexCount() const
        {
            uint32_t total = 0;
            for (const auto& lod : lodLevels) total += lod.vertexCount;
            return total;
        }

        uint32_t getTotalIndexCount() const
        {
            uint32_t total = 0;
            for (const auto& lod : lodLevels) total += lod.indexCount;
            return total;
        }
    };

    // Mesh metadata (submeshes, combined bounding box)
    struct MeshMetadata
    {
        std::vector<SubMeshMetadata> subMeshes;
        math::AABB boundingBox;

        int findSubmeshIndex(const std::string& name) const
        {
            for (size_t i = 0; i < subMeshes.size(); ++i)
            {
                if (subMeshes[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        uint32_t getTotalVertexCount() const
        {
            uint32_t total = 0;
            for (const auto& sub : subMeshes) total += sub.getTotalVertexCount();
            return total;
        }

        uint32_t getTotalIndexCount() const
        {
            uint32_t total = 0;
            for (const auto& sub : subMeshes) total += sub.getTotalIndexCount();
            return total;
        }
    };

    // Per-submesh material override
    struct SubMeshMaterialInfo
    {
        std::string materialPath; // Path to .vfMat file (empty = use default)
        // Override PBR values if no material file
        glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;
        uint8_t blendMode = 0; // 0=Opaque, 1=Masked, 2=Translucent
        float iblDiffuse = 1.0f;
        float iblSpecular = 0.5f;
    };

    struct MeshRenderData
    {
        std::string meshPath; // Path to identify loaded mesh
        glm::mat4 modelMatrix{1.0f}; // World transform

        // Default PBR values
        glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f}; // Base color (RGB + alpha)
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;

        // Texture indices for all 16 slots (-1.0 = no texture)
        // See ::material::TextureSlot for slot definitions
        std::array<float, ::material::MAX_MATERIAL_TEXTURES> textureIndices = {
            -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f
        };

        // Legacy accessors for common texture slots
        float& albedoTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Albedo)]; }
        float& normalTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Normal)]; }
        float& ormTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::ORM)]; }
        float& metallicTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Metallic)]; }
        float& roughnessTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Roughness)]; }
        float& aoTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::AO)]; }
        float& emissionTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Emission)]; }
        float& heightTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Height)]; }

        // Material assignments per submesh (keyed by submesh name)
        std::unordered_map<std::string, SubMeshMaterialInfo> submeshMaterials;
        std::string defaultMaterialPath;

        bool showBoundingBox = false; // Debug: render AABB wireframe
        int highlightedSubMesh = -1; // -1 = none, otherwise index of submesh to highlight

        float lodBias = 0.0f; // Shift LOD selection (+1 = lower quality, -1 = higher)
        int forceLODLevel = -1; // Force specific LOD (-1 = automatic selection)

        const SubMeshMaterialInfo* getMaterialForSubmesh(const std::string& submeshName) const
        {
            auto it = submeshMaterials.find(submeshName);
            return (it != submeshMaterials.end()) ? &it->second : nullptr;
        }
    };

    struct AABBPushConstants
    {
        glm::mat4 mvp; // 64 bytes - Model-View-Projection matrix
        glm::vec4 color; // 16 bytes - Wireframe color
    };

    struct CameraUBO
    {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::vec3 cameraPos;
        float time; // Animation time in seconds
    };

    
    struct MeshPushConstants
    {
        glm::mat4 model;    // 64 bytes
        glm::vec4 albedo;   // 16 bytes (RGB + alpha)
        float metallic;     // 4 bytes
        float roughness;    // 4 bytes
        float ao;           // 4 bytes
        float emission;     // 4 bytes

        uint32_t textureIndicesPacked[4]; // 16 bytes total for 16 texture indices

        float blendMode;    // 4 bytes (0=Opaque, 1=Masked, 2=Translucent)
        float iblDiffuse;   // 4 bytes
        float iblSpecular;  // 4 bytes

        // Helper to pack all 16 texture indices from float array
        void packTextureIndices(const std::array<float, 16>& indices) {
            for (int group = 0; group < 4; ++group) {
                textureIndicesPacked[group] = render::mesh::packTextureIndices(
                    floatToPackedIndex(indices[group * 4 + 0]),
                    floatToPackedIndex(indices[group * 4 + 1]),
                    floatToPackedIndex(indices[group * 4 + 2]),
                    floatToPackedIndex(indices[group * 4 + 3])
                );
            }
        }

        // Helper to set a single texture index
        void setTextureIndex(::material::TextureSlot slot, float index) {
            int slotIdx = ::material::toIndex(slot);
            int group = slotIdx / 4;
            int offset = slotIdx % 4;
            uint8_t packedIdx = floatToPackedIndex(index);

            // Clear the byte at this position and set new value
            textureIndicesPacked[group] &= ~(0xFF << (offset * 8));
            textureIndicesPacked[group] |= (static_cast<uint32_t>(packedIdx) << (offset * 8));
        }
    };

   
    struct MeshVertexInput
    {
        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = 32; // sizeof(Vertex): vec3 + vec3 + vec2
            bindingDescription.inputRate = vk::VertexInputRate::eVertex;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 3> attributes{};

            // location 0: position (vec3)
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = vk::Format::eR32G32B32Sfloat;
            attributes[0].offset = 0;

            // location 1: normal (vec3)
            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = vk::Format::eR32G32B32Sfloat;
            attributes[1].offset = 12;

            // location 2: texCoords (vec2)
            attributes[2].binding = 0;
            attributes[2].location = 2;
            attributes[2].format = vk::Format::eR32G32Sfloat;
            attributes[2].offset = 24;

            return attributes;
        }
    };
}
