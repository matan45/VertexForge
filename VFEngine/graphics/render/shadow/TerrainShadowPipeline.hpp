#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <glm/glm.hpp>

namespace core
{
    class Device;
    class Shader;
}

namespace render::shadow
{
    struct TerrainShadowPushConstants
    {
        glm::mat4 lightViewProjection;
        uint32_t tileCount;
        float depthBias;
        float slopeBias;
        // Target terrain LOD for this shadow page (clipmap level, clamped to the coarsest
        // shadow-searchable LOD). Must stay layout-identical to BOTH push blocks in
        // shadow_terrain.glsl.
        uint32_t terrainLod;
    };

    class TerrainShadowPipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> terrainShadowShader;
        vk::Pipeline terrainShadowPipeline;
        vk::PipelineLayout terrainShadowPipelineLayout;

        vk::DescriptorSetLayout cachedTerrainDataLayout;
        vk::DescriptorSetLayout cachedMeshletDataLayout;
        vk::DescriptorSetLayout cachedVertexDataLayout;

        vk::Format depthFormat = vk::Format::eD32Sfloat;
        bool initialized = false;

    public:
        explicit TerrainShadowPipeline(core::Device& device);
        ~TerrainShadowPipeline();

        TerrainShadowPipeline(const TerrainShadowPipeline&) = delete;
        TerrainShadowPipeline& operator=(const TerrainShadowPipeline&) = delete;

        void init(vk::DescriptorSetLayout terrainDataLayout,
                  vk::DescriptorSetLayout meshletDataLayout,
                  vk::DescriptorSetLayout vertexDataLayout,
                  vk::Format shadowDepthFormat);

        void cleanup();

        void dispatch(vk::CommandBuffer cmd,
                      vk::DescriptorSet terrainDataDescSet,
                      vk::DescriptorSet meshletDescSet,
                      vk::DescriptorSet vertexDescSet,
                      const glm::mat4& lightViewProjection,
                      uint32_t tileCount,
                      float depthBias,
                      float slopeBias,
                      uint32_t terrainLod);

        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createTerrainShadowPipeline();
    };
}
