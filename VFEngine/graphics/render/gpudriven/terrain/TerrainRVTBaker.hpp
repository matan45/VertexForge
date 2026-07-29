#pragma once

#include "../../virtualtexture/VTPhysicalPool.hpp"
#include "TerrainCompositePermutation.hpp"
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

// ============================================================================
// Virtual Texturing (VK-1209) — terrain RVT bake pipeline. A self-contained
// vertex+fragment graphics pipeline (resources/shaders/gpudriven/terrain_rvt_bake.glsl)
// that renders one quad per (page, overlapping terrain tile) into a page's atlas tile,
// running the SAME terrain splat composite the live pass uses and writing either the
// legacy 2-plane layout or the 4-plane normal/HDR-emission detail layout. Independent
// of the shared TerrainMeshShaderPipeline —
// it binds that pipeline's descriptor SETS at compact indices 0/1/2, so nothing about
// the default terrain path changes. GPUDrivenRenderer drives begin/beginPage/drawTile/end.
// ============================================================================

namespace render::gpudriven
{
    class TerrainRVTBaker
    {
    public:
        // Matches the BakePC push constant in terrain_rvt_bake.glsl (64 bytes).
        struct TilePush
        {
            glm::vec2 pageWorldMin{0.0f};   // page footprint incl. border (world XZ)
            glm::vec2 pageWorldSize{1.0f};
            glm::vec2 quadWorldMin{0.0f};    // this draw's quad = page ∩ terrain tile
            glm::vec2 quadWorldSize{1.0f};
            glm::vec2 tileWorldMin{0.0f};    // terrain tile origin (for per-tile UV)
            float tileWorldSize = 1.0f;
            uint32_t fragTileIndex = 0;      // terrain tile GPU index
            float textureScale = 0.1f;
            float pad0 = 0.0f;
            float pad1 = 0.0f;
            float pad2 = 0.0f;
        };

        explicit TerrainRVTBaker(core::Device& device);
        ~TerrainRVTBaker();

        TerrainRVTBaker(const TerrainRVTBaker&) = delete;
        TerrainRVTBaker& operator=(const TerrainRVTBaker&) = delete;

        void init(vk::DescriptorSetLayout weightMapLayout,   // set 0 (weightmap b0 + layers b1)
                  vk::DescriptorSetLayout bindlessLayout,    // set 1 (bindless b0)
                  vk::DescriptorSetLayout terrainDataLayout, // set 2 (tiles b0)
                  const std::vector<vk::Format>& planeFormats,
                  // MUST be TerrainMeshShaderPipeline::getCompositePermutation(). The bake and the
                  // live fallback compile the same generated composite; a mismatch shows up as a
                  // hard seam wherever a page is resident, and is invisible with RVT off. Taking
                  // the whole struct rather than loose bools is what makes that impossible to get
                  // half-right when a new permutation is added.
                  const TerrainCompositePermutation& permutation);
        void cleanup();
        [[nodiscard]] bool isReady() const { return graphicsPipeline != nullptr; }

        // Begin an MRT bake pass into the pool's planes. Caller must have transitioned the
        // pool to ColorAttachmentOptimal first (VTPhysicalPool::transition).
        void begin(vk::CommandBuffer cmd, const vt::VTPhysicalPool& pool,
                   vk::DescriptorSet weightMapSet, vk::DescriptorSet bindlessSet,
                   vk::DescriptorSet terrainDataSet) const;
        // Target one page's atlas tile: set viewport/scissor and clear it.
        void beginPage(vk::CommandBuffer cmd, const vt::VTPhysicalPool& pool, uint32_t tile) const;
        void drawTile(vk::CommandBuffer cmd, const TilePush& push) const;
        void end(vk::CommandBuffer cmd) const;

    private:
        core::Device& device;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
    };
}
