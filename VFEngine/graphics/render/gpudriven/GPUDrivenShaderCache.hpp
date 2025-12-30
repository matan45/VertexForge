#pragma once

#include <vulkan/vulkan.hpp>
#include <string>
#include <unordered_map>
#include <memory>
#include <set>


namespace core
{
    class Device;
    class Shader;
    class SwapChain;
}

namespace material
{
    struct MaterialData;
}

namespace render::gpudriven
{
    // Cached compiled shader and pipelines for a custom material in GPU-driven rendering
    struct GPUDrivenPipelineData
    {
        std::shared_ptr<core::Shader> shader;
        vk::Pipeline opaquePipeline;
        vk::Pipeline maskedPipeline;
        std::string vertexShaderHash;
        std::string fragmentShaderHash;
        uint32_t groupIndex = 0;
        bool valid = false;
    };


    class GPUDrivenShaderCache
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        vk::DescriptorSetLayout iblLayout;
        vk::DescriptorSetLayout perDrawDataLayout;
        vk::DescriptorSetLayout bindlessTextureLayout;
        vk::RenderPass renderPass;
        vk::PipelineLayout pipelineLayout; // Shared layout for all custom shaders

        bool initialized = false;

        std::unordered_map<std::string, GPUDrivenPipelineData> cache;

        std::unordered_map<std::string, uint32_t> materialToGroup;

        std::set<uint32_t> activeGroups;

        uint32_t nextGroupIndex = 1;

    public:
        explicit GPUDrivenShaderCache(core::Device& device, core::SwapChain& swapChain);
        ~GPUDrivenShaderCache();

        void init(vk::DescriptorSetLayout iblLayout,
                  vk::DescriptorSetLayout perDrawDataLayout,
                  vk::DescriptorSetLayout bindlessTextureLayout,
                  vk::RenderPass renderPass);

        uint32_t getOrCreateShaderGroup(const std::string& materialPath,
                                        const material::MaterialData& materialData);

        vk::Pipeline getPipeline(uint32_t shaderGroup, bool masked) const;

        const std::set<uint32_t>& getActiveGroups() const { return activeGroups; }

        void clearActiveGroups()
        {
            activeGroups.clear();
            activeGroups.insert(0);
        }

        void markGroupActive(uint32_t group) { activeGroups.insert(group); }

        void updateRenderPass(vk::RenderPass newRenderPass, vk::DescriptorSetLayout newIBLLayout = nullptr);

        void cleanup();

    private:
        void invalidateAll();

        bool compileAndCreatePipeline(const std::string& materialPath,
                                      const material::MaterialData& materialData,
                                      GPUDrivenPipelineData& outData);

        bool createPipelines(GPUDrivenPipelineData& data);

        static std::string hashShaderSource(const std::string& source);
    };
}
