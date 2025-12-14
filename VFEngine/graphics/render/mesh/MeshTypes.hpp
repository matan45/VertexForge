#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include "math/Frustum.hpp"
#include <array>
#include <string>
#include <unordered_map>

namespace material { enum class BlendMode : uint8_t; }

namespace render::mesh
{
    // Forward declare blend mode for use in render data
    using BlendMode = ::material::BlendMode;

    struct SubMeshGPUData
    {
        std::string name;  // Submesh name for material assignment
        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexBufferMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexBufferMemory;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
        math::AABB boundingBox;  // AABB for this submesh (local space)
    };

    // GPU-side mesh data containing all submeshes from a .vfmesh file
    struct MeshGPUData
    {
        std::vector<SubMeshGPUData> subMeshes;
        math::AABB boundingBox;  // Combined AABB of all submeshes (local space)

        // Find submesh index by name, returns -1 if not found
        int findSubmeshIndex(const std::string& name) const {
            for (size_t i = 0; i < subMeshes.size(); ++i) {
                if (subMeshes[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }
    };
    
    // Per-submesh material override
    struct SubMeshMaterialInfo
    {
        std::string materialPath;  // Path to .vfMat file (empty = use default)
        // Override PBR values if no material file
        glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;
        uint8_t blendMode = 0;  // 0=Opaque, 1=Masked, 2=Translucent
    };

    struct MeshRenderData
    {
        std::string meshPath;                              // Path to identify loaded mesh
        glm::mat4 modelMatrix{1.0f};                       // World transform

        // Default PBR values (used if no MaterialComponent or no material assigned)
        glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};         // Base color (RGB + alpha)
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;

        // Texture indices (-1.0 = no texture, 0-7 = index in u_Textures[8])
        float albedoTexIdx = -1.0f;
        float metallicTexIdx = -1.0f;
        float roughnessTexIdx = -1.0f;
        float aoTexIdx = -1.0f;
        float normalTexIdx = -1.0f;
        float emissionTexIdx = -1.0f;

        // Material assignments per submesh (keyed by submesh name)
        std::unordered_map<std::string, SubMeshMaterialInfo> submeshMaterials;
        std::string defaultMaterialPath;                   // Default material for unassigned submeshes

        bool showBoundingBox = false;                      // Debug: render AABB wireframe
        int highlightedSubMesh = -1;                       // -1 = none, otherwise index of submesh to highlight

        // Get material info for a submesh by name
        const SubMeshMaterialInfo* getMaterialForSubmesh(const std::string& submeshName) const {
            auto it = submeshMaterials.find(submeshName);
            return (it != submeshMaterials.end()) ? &it->second : nullptr;
        }
    };

    // Push constants for wireframe AABB rendering
    struct AABBPushConstants
    {
        glm::mat4 mvp;        // 64 bytes - Model-View-Projection matrix
        glm::vec4 color;      // 16 bytes - Wireframe color
    };
    
    struct CameraUBO
    {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::vec3 cameraPos;
        float time;  // Animation time in seconds
    };

    // Push constants - matches mesh.glsl push_constant block
    // Total size: 64 (mat4) + 16 (vec4) + 4*4 + 4*7 = 124 bytes (under 128 limit)
    struct MeshPushConstants
    {
        glm::mat4 model;      // 64 bytes
        glm::vec4 albedo;     // 16 bytes (RGB + alpha)
        float metallic;       // 4 bytes
        float roughness;      // 4 bytes
        float ao;             // 4 bytes
        float emission;       // 4 bytes

        // Texture indices: -1.0 = no texture, >= 0 = index in u_Textures[8]
        float albedoTexIdx;      // 4 bytes
        float metallicTexIdx;    // 4 bytes
        float roughnessTexIdx;   // 4 bytes
        float aoTexIdx;          // 4 bytes
        float normalTexIdx;      // 4 bytes
        float emissionTexIdx;    // 4 bytes
        float blendMode;         // 4 bytes (0=Opaque, 1=Masked, 2=Translucent)
    };

    // Vertex input helper matching resource::Vertex (32 bytes)
    // From utilities/resource/Types.hpp:
    //   position: vec3 at offset 0
    //   normal: vec3 at offset 12
    //   texCoords: vec2 at offset 24
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
