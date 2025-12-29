#include "MaterialShaderCache.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Utilities.hpp"
#include "material/MaterialTypes.hpp"
#include "print/Logger.hpp"
#include <functional>

namespace render::mesh
{
    MaterialShaderCache::MaterialShaderCache(core::Device& device)
        : device(device)
    {
    }

    MaterialShaderCache::~MaterialShaderCache()
    {
        cleanUp();
    }

    void MaterialShaderCache::init(vk::RenderPass renderPass,
                                   vk::PipelineLayout pipelineLayout,
                                   vk::Extent2D swapchainExtent)
    {
        this->renderPass = renderPass;
        this->pipelineLayout = pipelineLayout;
        this->swapchainExtent = swapchainExtent;
        initialized = true;
    }

    const MaterialPipelineData* MaterialShaderCache::getOrCreatePipeline(
        const std::string& materialPath,
        const material::MaterialData& materialData)
    {
        if (!initialized)
        {
            return nullptr;
        }
        
        if (materialData.cachedVertexShader.empty() || materialData.cachedFragmentShader.empty())
        {
            return nullptr;
        }
        
        auto it = cache.find(materialPath);
        if (it != cache.end())
        {
            std::string vsHash = hashShaderSource(materialData.cachedVertexShader);
            std::string fsHash = hashShaderSource(materialData.cachedFragmentShader);

            if (it->second.vertexShaderHash == vsHash &&
                it->second.fragmentShaderHash == fsHash &&
                it->second.valid)
            {
                return &it->second;
            }
            
            invalidate(materialPath);
        }
        
        MaterialPipelineData data;
        if (compileAndCreatePipeline(materialPath, materialData, data))
        {
            cache[materialPath] = std::move(data);
            return &cache[materialPath];
        }

        return nullptr;
    }

    bool MaterialShaderCache::compileAndCreatePipeline(
        const std::string& materialPath,
        const material::MaterialData& materialData,
        MaterialPipelineData& outData)
    {
        outData.shader = std::make_shared<core::Shader>(device);
        
        bool success = outData.shader->compileFromSources(
            materialData.cachedVertexShader,
            materialData.cachedFragmentShader,
            materialData.name
        );

        if (!success || outData.shader->getShaderStages().empty())
        {
            lastCompilationError = outData.shader->getLastCompilationError();
            loggerError("Failed to compile material shader: {}", materialPath);
            return false;
        }
        
        lastCompilationError.clear();
        
        outData.vertexShaderHash = hashShaderSource(materialData.cachedVertexShader);
        outData.fragmentShaderHash = hashShaderSource(materialData.cachedFragmentShader);
        
        if (!createPipelines(outData))
        {
            loggerError("Failed to create pipeline for material: {}", materialPath);
            return false;
        }

        outData.valid = true;
        loggerInfo("Compiled and cached material shader: {}", materialPath);
        return true;
    }

    bool MaterialShaderCache::createPipelines(MaterialPipelineData& data)
    {
        auto bindingDescription = MeshVertexInput::getBindingDescription();
        auto attributeDescriptions = MeshVertexInput::getAttributeDescriptions();

        try
        {
            core::GraphicsPipelineConfig config{
                .device = device.getLogicalDevice(),
                .renderPass = renderPass,
                .extent = swapchainExtent,
                .shaderStages = data.shader->getShaderStages(),
                .vertexBindings = {bindingDescription},
                .vertexAttributes = {attributeDescriptions.begin(), attributeDescriptions.end()},
                .existingPipelineLayout = pipelineLayout,
                .cullMode = vk::CullModeFlagBits::eBack,
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthCompareOp = vk::CompareOp::eLess
            };

            auto opaqueResult = core::Utilities::createGraphicsPipeline(config);
            data.opaquePipeline = opaqueResult.pipeline;

            // Masked pipeline (same as opaque for now)
            auto maskedResult = core::Utilities::createGraphicsPipeline(config);
            data.maskedPipeline = maskedResult.pipeline;

            return true;
        }
        catch (const std::exception& e)
        {
            loggerError("Failed to create material pipeline: {}", e.what());
            return false;
        }
    }

    void MaterialShaderCache::invalidate(const std::string& materialPath)
    {
        auto it = cache.find(materialPath);
        if (it != cache.end())
        {
            device.getLogicalDevice().waitIdle();

            if (it->second.opaquePipeline)
                device.getLogicalDevice().destroyPipeline(it->second.opaquePipeline);
            if (it->second.maskedPipeline)
                device.getLogicalDevice().destroyPipeline(it->second.maskedPipeline);

            if (it->second.shader)
                it->second.shader->cleanUp();

            cache.erase(it);
            loggerInfo("Invalidated material shader cache: {}", materialPath);
        }
    }

    void MaterialShaderCache::invalidateAll()
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
        loggerInfo("Invalidated all material shader caches");
    }

    void MaterialShaderCache::cleanUp()
    {
        if (!initialized) return;
        invalidateAll();
        initialized = false;
    }

    bool MaterialShaderCache::hasPipeline(const std::string& materialPath) const
    {
        auto it = cache.find(materialPath);
        return it != cache.end() && it->second.valid;
    }

    std::string MaterialShaderCache::hashShaderSource(const std::string& source)
    {
        // Simple hash using std::hash
        std::hash<std::string> hasher;
        return std::to_string(hasher(source));
    }
}
