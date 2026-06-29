#include "MaterialShaderCache.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "material/MaterialTypes.hpp"
#include "material/MaterialRuntimeData.hpp"
#include "resource/PathResolver.hpp"
#include "print/Log.hpp"
#include <functional>

namespace render::mesh
{
    vk::Pipeline MaterialPipelineData::pipelineForBlendMode(material::BlendMode blendMode) const
    {
        switch (blendMode)
        {
        case material::BlendMode::Masked: return maskedPipeline;
        case material::BlendMode::Translucent: return translucentPipeline ? translucentPipeline : opaquePipeline;
        case material::BlendMode::Additive: return additivePipeline ? additivePipeline : opaquePipeline;
        case material::BlendMode::Multiply: return multiplyPipeline ? multiplyPipeline : opaquePipeline;
        case material::BlendMode::Opaque:
        default:
            return opaquePipeline;
        }
    }

    MaterialShaderCache::MaterialShaderCache(core::Device& device)
        : device(device)
    {
    }

    MaterialShaderCache::~MaterialShaderCache()
    {
        cleanUp();
    }

    void MaterialShaderCache::init(vk::PipelineLayout pipelineLayout,
                                   vk::Extent2D swapchainExtent,
                                   vk::Format colorFormat,
                                   vk::Format depthFormat)
    {
        this->pipelineLayout = pipelineLayout;
        this->swapchainExtent = swapchainExtent;
        this->colorFormat = colorFormat;
        this->depthFormat = depthFormat;
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
            material::MaterialRuntimeData runtimeData =
                material::MaterialRuntimeDataBuilder::fromMaterialData(materialData);

            if (it->second.vertexShaderHash == runtimeData.shaderMap.vertexShaderHash &&
                it->second.fragmentShaderHash == runtimeData.shaderMap.fragmentShaderHash &&
                it->second.materialIRHash == runtimeData.irHash &&
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

        material::MaterialRuntimeData runtimeData =
            material::MaterialRuntimeDataBuilder::fromMaterialData(materialData);
        outData.vertexShaderHash = runtimeData.shaderMap.vertexShaderHash;
        outData.fragmentShaderHash = runtimeData.shaderMap.fragmentShaderHash;
        outData.materialIRHash = runtimeData.irHash;
        outData.shaderMapKey = runtimeData.shaderMap.shaderMapKey;

        if (resource::PathResolver::isExportedMode())
        {
            // In exported builds, load pre-compiled SPIR-V directly by hash
            std::string vfshaderPath = runtimeData.shaderMap.compiledShaderPath;

            if (!outData.shader->loadPrecompiledShader(vfshaderPath))
            {
                vfLogError("Failed to load pre-compiled material shader: {}", materialPath);
                return false;
            }
        }
        else
        {
            bool success = outData.shader->compileFromSources(
                materialData.cachedVertexShader,
                materialData.cachedFragmentShader,
                materialData.name
            );

            if (!success || outData.shader->getShaderStages().empty())
            {
                lastCompilationError = outData.shader->getLastCompilationError();
                vfLogError("Failed to compile material shader: {}", materialPath);
                return false;
            }
        }

        lastCompilationError.clear();

        if (!createPipelines(outData))
        {
            vfLogError("Failed to create pipeline for material: {}", materialPath);
            return false;
        }

        outData.valid = true;
        vfLogInfo("Compiled and cached material shader: {}", materialPath);
        return true;
    }

    bool MaterialShaderCache::createPipelines(MaterialPipelineData& data)
    {
        auto bindingDescription = MeshVertexInput::getBindingDescription();
        auto attributeDescriptions = MeshVertexInput::getAttributeDescriptions();

        try
        {
            auto makeConfig = [&](material::BlendMode mode)
            {
                core::GraphicsPipelineConfig config{
                .device = device.getLogicalDevice(),
                .extent = swapchainExtent,
                .colorAttachmentFormats = { colorFormat },
                .depthAttachmentFormat = depthFormat,
                .shaderStages = data.shader->getShaderStages(),
                .vertexBindings = {bindingDescription},
                .vertexAttributes = {attributeDescriptions.begin(), attributeDescriptions.end()},
                .existingPipelineLayout = pipelineLayout,
                .cullMode = vk::CullModeFlagBits::eBack,
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthCompareOp = vk::CompareOp::eLess,
                // Custom material pipelines render in both the dynamic-MSAA scene pass
                // and the single-sample preview/thumbnail pass, so they must carry
                // VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT. Without it the static
                // sample count mismatches the MSAA target and binding the pipeline
                // invalidates the pass's setRasterizationSamplesEXT for later draws.
                .dynamicSampleCount = true,
                };

                switch (mode)
                {
                case material::BlendMode::Translucent:
                    config.depthWriteEnable = false;
                    config.blendEnable = true;
                    config.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
                    config.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
                    config.srcAlphaBlendFactor = vk::BlendFactor::eOne;
                    config.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
                    break;
                case material::BlendMode::Additive:
                    config.depthWriteEnable = false;
                    config.blendEnable = true;
                    config.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
                    config.dstColorBlendFactor = vk::BlendFactor::eOne;
                    config.srcAlphaBlendFactor = vk::BlendFactor::eOne;
                    config.dstAlphaBlendFactor = vk::BlendFactor::eOne;
                    break;
                case material::BlendMode::Multiply:
                    config.depthWriteEnable = false;
                    config.blendEnable = true;
                    config.srcColorBlendFactor = vk::BlendFactor::eDstColor;
                    config.dstColorBlendFactor = vk::BlendFactor::eZero;
                    config.srcAlphaBlendFactor = vk::BlendFactor::eOne;
                    config.dstAlphaBlendFactor = vk::BlendFactor::eZero;
                    break;
                case material::BlendMode::Opaque:
                case material::BlendMode::Masked:
                default:
                    break;
                }
                return config;
            };

            auto opaqueResult = core::PipelineUtilities::createGraphicsPipeline(makeConfig(material::BlendMode::Opaque));
            data.opaquePipeline = opaqueResult.pipeline;

            auto maskedResult = core::PipelineUtilities::createGraphicsPipeline(makeConfig(material::BlendMode::Masked));
            data.maskedPipeline = maskedResult.pipeline;

            auto translucentResult = core::PipelineUtilities::createGraphicsPipeline(makeConfig(material::BlendMode::Translucent));
            data.translucentPipeline = translucentResult.pipeline;

            auto additiveResult = core::PipelineUtilities::createGraphicsPipeline(makeConfig(material::BlendMode::Additive));
            data.additivePipeline = additiveResult.pipeline;

            auto multiplyResult = core::PipelineUtilities::createGraphicsPipeline(makeConfig(material::BlendMode::Multiply));
            data.multiplyPipeline = multiplyResult.pipeline;

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to create material pipeline: {}", e.what());
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
            if (it->second.translucentPipeline)
                device.getLogicalDevice().destroyPipeline(it->second.translucentPipeline);
            if (it->second.additivePipeline)
                device.getLogicalDevice().destroyPipeline(it->second.additivePipeline);
            if (it->second.multiplyPipeline)
                device.getLogicalDevice().destroyPipeline(it->second.multiplyPipeline);

            if (it->second.shader)
                it->second.shader->cleanUp();

            cache.erase(it);
            vfLogInfo("Invalidated material shader cache: {}", materialPath);
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
            if (data.translucentPipeline)
                device.getLogicalDevice().destroyPipeline(data.translucentPipeline);
            if (data.additivePipeline)
                device.getLogicalDevice().destroyPipeline(data.additivePipeline);
            if (data.multiplyPipeline)
                device.getLogicalDevice().destroyPipeline(data.multiplyPipeline);

            if (data.shader)
                data.shader->cleanUp();
        }

        cache.clear();
        vfLogInfo("Invalidated all material shader caches");
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
        return material::MaterialRuntimeDataBuilder::shaderSourceHash(source);
    }
}
