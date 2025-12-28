#pragma once

#include <vulkan/vulkan.hpp>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <set>
#include "GPUDrivenTypes.hpp"

namespace core {
    class Device;
    class Shader;
    class SwapChain;
}

namespace material {
    struct MaterialData;
}

namespace render::gpudriven {

    // Cached compiled shader and pipelines for a custom material in GPU-driven rendering
    struct GPUDrivenPipelineData {
        std::shared_ptr<core::Shader> shader;
        vk::Pipeline opaquePipeline;
        vk::Pipeline maskedPipeline;
        std::string vertexShaderHash;
        std::string fragmentShaderHash;
        uint32_t groupIndex = 0;
        bool valid = false;
    };

    // Cache for custom material shaders in GPU-driven rendering
    // Manages shader groups for multi-pipeline indirect rendering
    class GPUDrivenShaderCache {
    public:
        GPUDrivenShaderCache(core::Device& device, core::SwapChain& swapChain);
        ~GPUDrivenShaderCache();

        // Initialize with GPU-driven descriptor set layouts
        void init(vk::DescriptorSetLayout iblLayout,
                  vk::DescriptorSetLayout perDrawDataLayout,
                  vk::DescriptorSetLayout bindlessTextureLayout,
                  vk::RenderPass renderPass);

        // Get or create shader group for a material
        // Returns 0 if material has no custom shader (use default pipeline)
        // Returns 1+ for custom shader groups
        uint32_t getOrCreateShaderGroup(const std::string& materialPath,
                                         const material::MaterialData& materialData);

        // Get pipeline for a shader group
        // Returns null pipeline if group is invalid
        vk::Pipeline getPipeline(uint32_t shaderGroup, bool masked) const;

        // Check if shader group is valid
        bool isValidGroup(uint32_t group) const;

        // Get all active shader groups (groups with objects this frame)
        const std::set<uint32_t>& getActiveGroups() const { return activeGroups; }

        // Clear active groups (called at start of frame)
        void clearActiveGroups() { activeGroups.clear(); activeGroups.insert(0); }

        // Mark a shader group as active for this frame
        void markGroupActive(uint32_t group) { activeGroups.insert(group); }

        // Invalidate cached pipeline for a material
        void invalidate(const std::string& materialPath);

        // Invalidate all cached pipelines
        void invalidateAll();

        // Update render pass and optionally IBL layout (call when swapchain is recreated)
        // This invalidates all cached pipelines since they need to be recreated
        void updateRenderPass(vk::RenderPass newRenderPass, vk::DescriptorSetLayout newIBLLayout = nullptr);

        // Clean up all resources
        void cleanup();

        // Get last compilation error
        const std::string& getLastCompilationError() const { return lastCompilationError; }

    private:
        core::Device& device;
        core::SwapChain& swapChain;

        vk::DescriptorSetLayout iblLayout;
        vk::DescriptorSetLayout perDrawDataLayout;
        vk::DescriptorSetLayout bindlessTextureLayout;
        vk::RenderPass renderPass;
        vk::PipelineLayout pipelineLayout;  // Shared layout for all custom shaders

        bool initialized = false;
        std::string lastCompilationError;

        // Cache: material path -> pipeline data
        std::unordered_map<std::string, GPUDrivenPipelineData> cache;

        // Material path -> shader group index
        std::unordered_map<std::string, uint32_t> materialToGroup;

        // Active shader groups for current frame
        std::set<uint32_t> activeGroups;

        // Next available group index (0 is reserved for default PBR)
        uint32_t nextGroupIndex = 1;

        // Compile shader and create pipeline
        // targetGroupIndex is baked into the shader for group filtering
        bool compileAndCreatePipeline(const std::string& materialPath,
                                       const material::MaterialData& materialData,
                                       uint32_t targetGroupIndex,
                                       GPUDrivenPipelineData& outData);

        // Create pipeline from compiled shader
        bool createPipelines(GPUDrivenPipelineData& data);

        // Generate hash for shader source
        static std::string hashShaderSource(const std::string& source);

        // Generate GPU-driven compatible vertex shader
        std::string generateGPUDrivenVertexShader() const;

        // Generate GPU-driven compatible fragment shader with group filtering
        // expectedGroupIndex is baked into shader for multi-pipeline rendering
        std::string generateGPUDrivenFragmentShader(uint32_t expectedGroupIndex) const;
    };

}
