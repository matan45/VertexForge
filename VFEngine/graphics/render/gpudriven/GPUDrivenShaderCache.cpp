#include "GPUDrivenShaderCache.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "GPUDrivenTypes.hpp"
#include "material/MaterialTypes.hpp"
#include "print/Logger.hpp"
#include <array>
#include <functional>

namespace render::gpudriven
{
    GPUDrivenShaderCache::GPUDrivenShaderCache(core::Device& device, core::SwapChain& swapChain)
        : device(device)
          , swapChain(swapChain)
    {
        // Group 0 is always active (default PBR)
        activeGroups.insert(0);
    }

    GPUDrivenShaderCache::~GPUDrivenShaderCache()
    {
        cleanup();
    }

    void GPUDrivenShaderCache::init(vk::DescriptorSetLayout iblLayout,
                                    vk::DescriptorSetLayout perDrawDataLayout,
                                    vk::DescriptorSetLayout bindlessTextureLayout,
                                    vk::RenderPass renderPass)
    {
        this->iblLayout = iblLayout;
        this->perDrawDataLayout = perDrawDataLayout;
        this->bindlessTextureLayout = bindlessTextureLayout;
        this->renderPass = renderPass;

        // Create shared pipeline layout for all custom shaders
        // Same layout as default GPU-driven pipeline:
        // Set 0: IBL (camera UBO + irradiance + prefilter + brdfLUT)
        // Set 1: Per-draw data storage buffer
        // Set 2: Bindless textures
        std::array<vk::DescriptorSetLayout, 3> setLayouts = {
            this->iblLayout,
            this->perDrawDataLayout,
            this->bindlessTextureLayout
        };

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        pipelineLayout = device.getLogicalDevice().createPipelineLayout(layoutInfo);
        initialized = true;

        loggerInfo("GPUDrivenShaderCache: Initialized with shared pipeline layout");
    }

    uint32_t GPUDrivenShaderCache::getOrCreateShaderGroup(const std::string& materialPath,
                                                          const material::MaterialData& materialData)
    {
        if (!initialized)
        {
            return 0;
        }

        auto groupIt = materialToGroup.find(materialPath);
        if (groupIt != materialToGroup.end())
        {
            return groupIt->second;
        }

        // Check if material has custom shaders
        // Only consider it custom if BOTH vertex and fragment shaders are present
        if (materialData.cachedVertexShader.empty() || materialData.cachedFragmentShader.empty())
        {
            return 0;
        }

        // Check if the shader graph contains nodes that require custom shader handling
        bool hasCustomLogic = false;
        for (const auto& node : materialData.graph.nodes)
        {
            if (node.type == material::NodeType::Time)
            {
                hasCustomLogic = true;
                break;
            }
        }

        if (!hasCustomLogic)
        {
            // Material has cached shaders but they're just standard PBR, use default pipeline
            // Cache this result so we don't check again
            materialToGroup[materialPath] = 0;
            return 0;
        }

        // Create new pipeline
        // Determine target group index BEFORE compilation (for shader group filtering)
        uint32_t targetGroupIndex = nextGroupIndex;
        if (targetGroupIndex >= MAX_SHADER_GROUPS)
        {
            loggerWarning("GPUDrivenShaderCache: Max shader groups reached ({}), reusing last group",
                          MAX_SHADER_GROUPS);
            targetGroupIndex = MAX_SHADER_GROUPS - 1;
        }

        GPUDrivenPipelineData data;
        if (compileAndCreatePipeline(materialPath, materialData, data))
        {
            // Assign group index and increment counter
            data.groupIndex = targetGroupIndex;
            if (nextGroupIndex < MAX_SHADER_GROUPS - 1)
            {
                nextGroupIndex++;
            }

            cache[materialPath] = std::move(data);
            materialToGroup[materialPath] = cache[materialPath].groupIndex;

            loggerInfo("GPUDrivenShaderCache: Created shader group {} for material: {}",
                       cache[materialPath].groupIndex, materialPath);

            return cache[materialPath].groupIndex;
        }

        loggerWarning("GPUDrivenShaderCache: Failed to compile custom shader for: {}, using default", materialPath);
        return 0;
    }

    vk::Pipeline GPUDrivenShaderCache::getPipeline(uint32_t shaderGroup, bool masked) const
    {
        if (shaderGroup == 0)
        {
            return nullptr;
        }

        for (const auto& [path, data] : cache)
        {
            if (data.groupIndex == shaderGroup && data.valid)
            {
                return masked ? data.maskedPipeline : data.opaquePipeline;
            }
        }

        return nullptr;
    }

    bool GPUDrivenShaderCache::compileAndCreatePipeline(const std::string& materialPath,
                                                        const material::MaterialData& materialData,
                                                        GPUDrivenPipelineData& outData)
    {
        outData.shader = std::make_shared<core::Shader>(device);
        outData.shader->readShader("../../resources/shaders/gpudriven/custom_gpudriven.glsl");

        if (outData.shader->getShaderStages().empty())
        {
            loggerError("GPUDrivenShaderCache: Failed to load shader for: {}", materialPath);
            return false;
        }

        outData.vertexShaderHash = hashShaderSource(materialData.cachedVertexShader);
        outData.fragmentShaderHash = hashShaderSource(materialData.cachedFragmentShader);

        if (!createPipelines(outData))
        {
            loggerError("GPUDrivenShaderCache: Failed to create pipeline for: {}", materialPath);
            return false;
        }

        outData.valid = true;
        return true;
    }

    bool GPUDrivenShaderCache::createPipelines(GPUDrivenPipelineData& data)
    {
        auto bindingDescription = mesh::MeshVertexInput::getBindingDescription();
        auto attributeDescriptions = mesh::MeshVertexInput::getAttributeDescriptions();

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = data.shader->getShaderStages(),
            .vertexBindings = {bindingDescription},
            .vertexAttributes = {attributeDescriptions.begin(), attributeDescriptions.end()},
            .existingPipelineLayout = pipelineLayout,
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthWriteEnable = true
        };

        try
        {
            auto opaqueResult = core::PipelineUtilities::createGraphicsPipeline(config);
            data.opaquePipeline = opaqueResult.pipeline;

            // Masked pipeline (same as opaque for now)
            auto maskedResult = core::PipelineUtilities::createGraphicsPipeline(config);
            data.maskedPipeline = maskedResult.pipeline;

            return true;
        }
        catch (const std::exception& e)
        {
            loggerError("GPUDrivenShaderCache: Failed to create pipeline - {}", e.what());
            return false;
        }
    }

    void GPUDrivenShaderCache::invalidateAll()
    {
        if (cache.empty()) return;

        device.getLogicalDevice().waitIdle();

        for (auto& [path, data] : cache)
        {
            if (data.opaquePipeline)
                device.getLogicalDevice().destroyPipeline(data.opaquePipeline);
            if (data.maskedPipeline)
                device.getLogicalDevice().destroyPipeline(data.maskedPipeline);

            if (data.shader)
                data.shader->cleanUp();
        }

        cache.clear();
        materialToGroup.clear();
        nextGroupIndex = 1; // Reset group allocation

        loggerInfo("GPUDrivenShaderCache: Invalidated all caches");
    }

    void GPUDrivenShaderCache::updateRenderPass(vk::RenderPass newRenderPass, vk::DescriptorSetLayout newIBLLayout)
    {
        if (!initialized) return;

        bool renderPassChanged = (renderPass != newRenderPass);
        bool iblLayoutChanged = (newIBLLayout && iblLayout != newIBLLayout);

        if (renderPassChanged || iblLayoutChanged)
        {
            loggerInfo("GPUDrivenShaderCache: Updating render pass/IBL layout, invalidating cached pipelines");

            invalidateAll();

            if (iblLayoutChanged && pipelineLayout)
            {
                device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

                std::array<vk::DescriptorSetLayout, 3> setLayouts = {
                    newIBLLayout,
                    perDrawDataLayout,
                    bindlessTextureLayout
                };

                vk::PipelineLayoutCreateInfo layoutInfo{};
                layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
                layoutInfo.pSetLayouts = setLayouts.data();
                layoutInfo.pushConstantRangeCount = 0;
                layoutInfo.pPushConstantRanges = nullptr;

                pipelineLayout = device.getLogicalDevice().createPipelineLayout(layoutInfo);
                iblLayout = newIBLLayout;
            }

            renderPass = newRenderPass;
        }
    }

    void GPUDrivenShaderCache::cleanup()
    {
        if (!initialized) return;

        invalidateAll();

        if (pipelineLayout)
        {
            device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        initialized = false;
    }

    std::string GPUDrivenShaderCache::hashShaderSource(const std::string& source)
    {
        std::hash<std::string> hasher;
        return std::to_string(hasher(source));
    }
}
