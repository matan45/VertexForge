#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::shadow::vsm
{
    inline constexpr uint32_t PAGE_SIZE = 128;
    inline constexpr uint32_t PHYSICAL_POOL_DIM = 8192;
    inline constexpr uint32_t MAX_PHYSICAL_TILES = (PHYSICAL_POOL_DIM / PAGE_SIZE) * (PHYSICAL_POOL_DIM / PAGE_SIZE); // 4096
    inline constexpr uint32_t TILES_PER_SIDE = PHYSICAL_POOL_DIM / PAGE_SIZE; // 64
    inline constexpr uint32_t VIRTUAL_SM_DIM = 16384;
    inline constexpr uint32_t PAGES_PER_SIDE = VIRTUAL_SM_DIM / PAGE_SIZE; // 128
    inline constexpr uint32_t INVALID_TILE = 0xFFFFFFFF;
    inline constexpr uint32_t MAX_VSM_LIGHTS = 64;

    // GPU page table entry (packed uint32_t)
    // Bits [0..5]:  physical tile X (0..63)
    // Bits [6..11]: physical tile Y (0..63)
    // Bit  [31]:    valid flag
    inline constexpr uint32_t PAGE_ENTRY_VALID_BIT = 0x80000000u;
    inline constexpr uint32_t PAGE_ENTRY_X_MASK = 0x3Fu;
    inline constexpr uint32_t PAGE_ENTRY_Y_SHIFT = 6;
    inline constexpr uint32_t PAGE_ENTRY_Y_MASK = 0x3Fu;

    inline uint32_t packPageEntry(uint32_t tileX, uint32_t tileY)
    {
        return PAGE_ENTRY_VALID_BIT | (tileX & PAGE_ENTRY_X_MASK) | ((tileY & PAGE_ENTRY_Y_MASK) << PAGE_ENTRY_Y_SHIFT);
    }

    inline void unpackPageEntry(uint32_t entry, uint32_t& tileX, uint32_t& tileY)
    {
        tileX = entry & PAGE_ENTRY_X_MASK;
        tileY = (entry >> PAGE_ENTRY_Y_SHIFT) & PAGE_ENTRY_Y_MASK;
    }

    inline bool isPageValid(uint32_t entry)
    {
        return (entry & PAGE_ENTRY_VALID_BIT) != 0;
    }

    // GPU shadow light data - replaces GPUShadowData
    struct alignas(16) GPUVSMLight
    {
        glm::mat4 viewProjection;       // full light VP
        glm::vec4 biasParams;           // depthBias, slopeBias, normalBias, texelSize
        glm::vec4 rangeParams;          // near, far, cascadeCount/unused, cascadeIndex/unused
        glm::vec4 pcssParams;           // lightSize, searchRadius, filterEnabled, cubeMapIndex
        glm::ivec4 pageTableInfo;       // pagesX, pagesY, pageTableOffset, lightType (0=dir, 1=spot, 2=point)
    };
    static_assert(sizeof(GPUVSMLight) == 128, "GPUVSMLight must be 128 bytes");

    // Per-page render info - CPU-side tracking
    struct VSMPageRenderInfo
    {
        uint32_t physicalTileIndex = INVALID_TILE;
        glm::mat4 cropViewProjection{1.0f};
        bool dirty = true;
    };

    // Per-light page allocation tracking
    struct VSMLightPages
    {
        uint32_t lightIndex = 0;           // index in GPUVSMLight array
        uint32_t pagesX = 0;
        uint32_t pagesY = 0;
        uint32_t pageTableOffset = 0;      // offset in page table buffer
        std::vector<uint32_t> physicalTiles; // physical tile index per page
        std::vector<bool> dirty;             // per-page dirty flag
        glm::mat4 lightViewProjection{1.0f}; // full light VP
    };

    // Compute the crop matrix that zooms into a specific page region
    inline glm::mat4 computePageCropMatrix(uint32_t pageX, uint32_t pageY, uint32_t pagesX, uint32_t pagesY)
    {
        float scaleX = static_cast<float>(pagesX);
        float scaleY = static_cast<float>(pagesY);
        float offsetX = -1.0f + (2.0f * static_cast<float>(pageX) + 1.0f) / scaleX;
        float offsetY = -1.0f + (2.0f * static_cast<float>(pageY) + 1.0f) / scaleY;

        // Scale and translate NDC to zoom into the page region
        glm::mat4 crop(1.0f);
        crop[0][0] = scaleX;
        crop[1][1] = scaleY;
        crop[3][0] = -offsetX * scaleX;
        crop[3][1] = -offsetY * scaleY;
        return crop;
    }
}
