#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>

namespace render::billboard
{
    
    struct BillboardVertex
    {
        glm::vec2 position;   // Quad corner offset (-0.5 to 0.5)
        glm::vec2 texCoord;  

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(BillboardVertex);
            bindingDescription.inputRate = vk::VertexInputRate::eVertex;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 2> attributes{};

            // location 0: position (vec2)
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = vk::Format::eR32G32Sfloat;
            attributes[0].offset = offsetof(BillboardVertex, position);

            // location 1: texCoord (vec2)
            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = vk::Format::eR32G32Sfloat;
            attributes[1].offset = offsetof(BillboardVertex, texCoord);

            return attributes;
        }
    };

   
    struct BillboardInstanceData
    {
        glm::vec3 worldPosition;  // World position of billboard center
        float atlasIndex;         // Index into texture atlas (as float for shader)
        glm::vec2 size;           // Size in pixels (screen-space) or world units
        uint32_t sizeMode;        // 0 = ScreenSpace, 1 = WorldSpace
        uint32_t entityId;        // Entity ID for picking
        glm::vec4 colorTint;      

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 1;
            bindingDescription.stride = sizeof(BillboardInstanceData);
            bindingDescription.inputRate = vk::VertexInputRate::eInstance;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 4> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 4> attributes{};

            // location 2: worldPosition (vec3) + atlasIndex (float) packed as vec4
            attributes[0].binding = 1;
            attributes[0].location = 2;
            attributes[0].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[0].offset = offsetof(BillboardInstanceData, worldPosition);

            // location 3: size (vec2)
            attributes[1].binding = 1;
            attributes[1].location = 3;
            attributes[1].format = vk::Format::eR32G32Sfloat;
            attributes[1].offset = offsetof(BillboardInstanceData, size);

            // location 4: sizeMode (uint)
            attributes[2].binding = 1;
            attributes[2].location = 4;
            attributes[2].format = vk::Format::eR32Uint;
            attributes[2].offset = offsetof(BillboardInstanceData, sizeMode);

            // location 5: colorTint (vec4)
            attributes[3].binding = 1;
            attributes[3].location = 5;
            attributes[3].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[3].offset = offsetof(BillboardInstanceData, colorTint);

            return attributes;
        }
    };
    
    struct BillboardCameraUBO
    {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::vec3 cameraPos;
        float padding;
    };
    
    struct BillboardPushConstants
    {
        glm::vec2 viewportSize;    
        float atlasGridSize;        // Number of tiles per row/column in atlas (e.g., 4 for 4x4)
        float padding;
    };

   
    struct BillboardRenderData
    {
        glm::vec3 worldPosition;
        uint32_t atlasIndex;
        glm::vec2 size;
        uint32_t sizeMode;      // 0 = ScreenSpace, 1 = WorldSpace
        uint32_t entityId;
        glm::vec4 colorTint;
        std::string texturePath; // Non-empty = use custom texture instead of atlas
    };

    // Atlas configuration
    struct AtlasConfig
    {
        static constexpr uint32_t GRID_SIZE = 4;           
        static constexpr uint32_t TILE_SIZE = 64;        
        static constexpr uint32_t ATLAS_SIZE = GRID_SIZE * TILE_SIZE;  
        static constexpr uint32_t MAX_ICONS = GRID_SIZE * GRID_SIZE;  
    };
}
