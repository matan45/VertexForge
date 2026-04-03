#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "math/Frustum.hpp"
#include "resource/Types.hpp"
#include "material/MaterialTypes.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <array>
#include <cstddef>
#include "../common/CameraTypes.hpp"
#include <string>
#include <unordered_map>

namespace material
{
    enum class BlendMode : uint8_t;
}

namespace render::mesh
{
    constexpr uint8_t TEXTURE_INDEX_NONE = 255;

    inline uint32_t packTextureIndices(uint8_t idx0, uint8_t idx1, uint8_t idx2, uint8_t idx3) {
        return static_cast<uint32_t>(idx0) |
               (static_cast<uint32_t>(idx1) << 8) |
               (static_cast<uint32_t>(idx2) << 16) |
               (static_cast<uint32_t>(idx3) << 24);
    }

    inline uint8_t floatToPackedIndex(float idx) {
        return (idx < 0.0f) ? TEXTURE_INDEX_NONE : static_cast<uint8_t>(idx);
    }
}

namespace render::mesh
{
    using BlendMode = ::material::BlendMode;

    struct LODGPUBuffers
    {
        vk::Buffer vertexBuffer;
        core::VulkanAllocation vertexBufferAllocation;
        vk::Buffer indexBuffer;
        core::VulkanAllocation indexBufferAllocation;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;

        bool isValid() const { return indexCount > 0 || vertexCount > 0; }
    };

    
    struct SubMeshGPUData
    {
        std::string name;
        std::array<LODGPUBuffers, resource::LOD_LEVEL_COUNT> lodLevels;
        math::AABB boundingBox;

        const LODGPUBuffers& getLOD(uint32_t level) const
        {
            return lodLevels[std::min(level, resource::LOD_LEVEL_COUNT - 1)];
        }
    };


    struct MeshGPUData
    {
        std::vector<SubMeshGPUData> subMeshes;
        math::AABB boundingBox;

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

    struct SubMeshMaterialInfo
    {
        std::string materialPath;
        glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;
        uint8_t blendMode = 0;
        float opacity = 1.0f;
        float alphaCutoff = 0.5f;
        float iblDiffuse = 1.0f;
        float iblSpecular = 0.5f;
    };

    struct MeshRenderData
    {
        entt::entity entity = entt::null;
        std::string meshPath;
        glm::mat4 modelMatrix{1.0f};

        glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;

        std::array<float, ::material::MAX_MATERIAL_TEXTURES> textureIndices = {
            -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f
        };

        float& albedoTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Albedo)]; }
        float& normalTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Normal)]; }
        float& ormTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::ORM)]; }
        float& metallicTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Metallic)]; }
        float& roughnessTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Roughness)]; }
        float& aoTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::AO)]; }
        float& emissionTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Emission)]; }
        float& heightTexIdx() { return textureIndices[::material::toIndex(::material::TextureSlot::Height)]; }

        std::unordered_map<std::string, SubMeshMaterialInfo> submeshMaterials;
        std::string defaultMaterialPath;

        bool showBoundingBox = false;
        bool wireframeMode = false;
        int materialOverrideMode = 0; // 0=default, 1=clay, 2=normals, 3=UVs
        int highlightedSubMesh = -1;

        float lodBias = 0.0f;
        int forceLODLevel = -1;
        float maxDrawDistance = 0.0f;
        bool isStatic = true;
        int32_t submeshIndex = -1; // -1 = all, >= 0 = only this submesh

        // Instance batching: if non-empty, render N instances with different transforms
        struct InstanceData
        {
            glm::mat4 modelMatrix{1.0f};
            glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
            glm::vec4 pbrParams{0.0f, 0.5f, 1.0f, 0.0f}; // metallic, roughness, ao, emission
            glm::vec4 iblParams{1.0f, 0.5f, 0.0f, 0.0f};  // iblDiffuse, iblSpecular, alphaCutoff, hasOverride
        };
        std::vector<InstanceData> instanceTransforms;

        const SubMeshMaterialInfo* getMaterialForSubmesh(const std::string& submeshName) const
        {
            auto it = submeshMaterials.find(submeshName);
            return (it != submeshMaterials.end()) ? &it->second : nullptr;
        }
    };

    struct AABBPushConstants
    {
        glm::mat4 mvp;
        glm::vec4 color;
    };

    using CameraUBO = ::render::common::CameraUBO;

    struct MeshPushConstants
    {
        glm::mat4 model;
        glm::vec4 albedo;
        float metallic;
        float roughness;
        float ao;
        float emission;

        uint32_t textureIndicesPacked[4];

        float blendMode;
        float alphaCutoff;
        float iblDiffuse;
        float iblSpecular;

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

        void setTextureIndex(::material::TextureSlot slot, float index) {
            int slotIdx = ::material::toIndex(slot);
            int group = slotIdx / 4;
            int offset = slotIdx % 4;
            uint8_t packedIdx = floatToPackedIndex(index);

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
            bindingDescription.stride = 64;
            bindingDescription.inputRate = vk::VertexInputRate::eVertex;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 3> attributes{};

            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = vk::Format::eR32G32B32Sfloat;
            attributes[0].offset = 0;

            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = vk::Format::eR32G32B32Sfloat;
            attributes[1].offset = 12;

            attributes[2].binding = 0;
            attributes[2].location = 2;
            attributes[2].format = vk::Format::eR32G32Sfloat;
            attributes[2].offset = 24;

            return attributes;
        }
    };
}
