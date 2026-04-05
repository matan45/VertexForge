#include "MaterialShaderCache.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "material/MaterialTypes.hpp"
#include "resource/PathResolver.hpp"
#include "print/Log.hpp"
#include "archive/VFPakFormat.hpp"
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

        outData.vertexShaderHash = hashShaderSource(materialData.cachedVertexShader);
        outData.fragmentShaderHash = hashShaderSource(materialData.cachedFragmentShader);

        if (resource::PathResolver::isExportedMode())
        {
            // In exported builds, load pre-compiled SPIR-V directly by hash
            std::string combinedHash = outData.vertexShaderHash + "_" + outData.fragmentShaderHash;
            std::string vfshaderPath = "Assets/materials/compiled/" + combinedHash + ".vfshader";

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
            };

            auto opaqueResult = core::PipelineUtilities::createGraphicsPipeline(config);
            data.opaquePipeline = opaqueResult.pipeline;

            // Masked pipeline (same as opaque for now)
            auto maskedResult = core::PipelineUtilities::createGraphicsPipeline(config);
            data.maskedPipeline = maskedResult.pipeline;

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
        uint64_t hash = archive::hashPath(source);
        char buf[17];
        snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
        return std::string(buf);
    }
}
