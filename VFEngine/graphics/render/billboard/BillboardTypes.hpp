#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include "../common/CameraTypes.hpp"

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
        glm::vec4 animParams0;    // x=cols y=rows z=frameRate w=spinSpeed
        glm::vec4 animParams1;    // x=scrollU y=scrollV z=pulseAmp w=pulseFreq
        float animStartTime;      // animation time origin (engine seconds)
        float loopAnim;           // 1.0 = loop flipbook, 0.0 = play once then hold last frame

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 1;
            bindingDescription.stride = sizeof(BillboardInstanceData);
            bindingDescription.inputRate = vk::VertexInputRate::eInstance;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 8> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 8> attributes{};

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

            // location 6: animParams0 (vec4) - x=cols y=rows z=frameRate w=spinSpeed
            attributes[4].binding = 1;
            attributes[4].location = 6;
            attributes[4].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[4].offset = offsetof(BillboardInstanceData, animParams0);

            // location 7: animParams1 (vec4) - x=scrollU y=scrollV z=pulseAmp w=pulseFreq
            attributes[5].binding = 1;
            attributes[5].location = 7;
            attributes[5].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[5].offset = offsetof(BillboardInstanceData, animParams1);

            // location 8: animStartTime (float)
            attributes[6].binding = 1;
            attributes[6].location = 8;
            attributes[6].format = vk::Format::eR32Sfloat;
            attributes[6].offset = offsetof(BillboardInstanceData, animStartTime);

            // location 9: loopAnim (float) - 1.0 = loop, 0.0 = play once
            attributes[7].binding = 1;
            attributes[7].location = 9;
            attributes[7].format = vk::Format::eR32Sfloat;
            attributes[7].offset = offsetof(BillboardInstanceData, loopAnim);

            return attributes;
        }
    };
    
    using BillboardCameraUBO = render::common::CameraUBO;
    
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
        float atlasGridSize = 1.0f; // For impostor atlases: number of columns in grid

        // Animation (Phase 1). Defaults render identically to a static billboard.
        uint32_t flipbookColumns = 1;
        uint32_t flipbookRows = 1;
        float flipbookFrameRate = 0.0f;
        float scrollU = 0.0f;
        float scrollV = 0.0f;
        float pulseAmplitude = 0.0f;
        float pulseFrequency = 0.0f;
        float spinSpeed = 0.0f;
        float animStartTime = 0.0f;
        bool loopAnimation = true; // flipbook: true=loop, false=play once then hold last frame
        bool worldMarker = false;
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
