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
#include <cassert>

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

        // The compiled pipeline is fully determined by the vertex + fragment SPIR-V.
        // Blend modes are all pre-created (selected at bind via pipelineForBlendMode) and
        // opacity/alphaCutoff are runtime uniforms, so material IR changes that don't alter
        // the shader text don't invalidate the pipeline. The old irHash check compared the
        // persisted materialData.irHash field, which goes stale on in-memory IR edits (e.g.
        // switching shadingModel to Toon) and made getOrCreatePipeline recompile every frame
        // (VK-1493). A shader-affecting change (graph edit, toon defines) already changes the
        // fragment hash, so dropping the irHash check keeps recompiles correct.
        const std::string vertexShaderHash = hashShaderSource(materialData.cachedVertexShader);
        const std::string fragmentShaderHash = hashShaderSource(materialData.cachedFragmentShader);

        bool hadEntry = false;
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = cache.find(materialPath);
            if (it != cache.end())
            {
                hadEntry = true;
                if (it->second.vertexShaderHash == vertexShaderHash &&
                    it->second.fragmentShaderHash == fragmentShaderHash &&
                    it->second.valid)
                {
                    return &it->second;
                }
                // Shaders changed under this path — drop the stale pipeline and rebuild below.
                invalidateLocked(materialPath);
            }
        }

        // About to build a NEW pipeline synchronously (no prior entry). If a frame is being
        // recorded on the render thread, warm-up (VK-1532) failed to cover this permutation and
        // we are hitching the frame. Always log; assert in Debug (no-op under NDEBUG). A rebuild
        // of an existing entry (hadEntry) is an expected editor live-edit, not a warm-up miss,
        // so it does not trip the guard.
        if (!hadEntry && frameRecordingActive.load(std::memory_order_acquire))
        {
            vfLogWarning("VK-1532: pipeline for '{}' created on the render thread mid-frame "
                         "- warm-up missed this permutation", materialPath);
            assert(!"VK-1532: pipeline created on render thread mid-frame");
        }

        MaterialPipelineData data;
        std::string compileError;
        const bool ok = compileAndCreatePipeline(materialPath, materialData, data, compileError);
        lastCompilationError = std::move(compileError);
        if (!ok)
        {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(cacheMutex);
        // A warm-up job may have inserted a matching valid entry while we were building
        // off-lock; prefer it and discard our duplicate so we don't leak pipelines.
        auto it = cache.find(materialPath);
        if (it != cache.end() && it->second.valid &&
            it->second.vertexShaderHash == vertexShaderHash &&
            it->second.fragmentShaderHash == fragmentShaderHash)
        {
            destroyPipelineData(data);
            return &it->second;
        }
        if (it != cache.end())
        {
            invalidateLocked(materialPath);
        }
        cache[materialPath] = std::move(data);
        return &cache[materialPath];
    }

    bool MaterialShaderCache::warmPipeline(const std::string& materialPath,
                                           const material::MaterialData& materialData)
    {
        if (!initialized)
        {
            return false;
        }
        if (materialData.cachedVertexShader.empty() || materialData.cachedFragmentShader.empty())
        {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            if (hasPipelineLocked(materialPath))
            {
                return true; // already warm
            }
        }

        // Heavy work off-lock (shader compile in editor mode + 5 pipeline creates). Safe to
        // run concurrently with other warm-up jobs and the render thread.
        MaterialPipelineData data;
        std::string compileError;
        if (!compileAndCreatePipeline(materialPath, materialData, data, compileError))
        {
            // Intentionally do not touch lastCompilationError from a worker thread.
            return false;
        }

        std::lock_guard<std::mutex> lock(cacheMutex);
        if (cache.find(materialPath) != cache.end())
        {
            // Another thread already produced an entry for this path while we built. Never
            // destroy an existing (possibly in-use) pipeline from a worker thread — just drop
            // our freshly-built, never-bound duplicate.
            destroyPipelineData(data);
            return true;
        }
        cache[materialPath] = std::move(data);
        return true;
    }

    bool MaterialShaderCache::compileAndCreatePipeline(
        const std::string& materialPath,
        const material::MaterialData& materialData,
        MaterialPipelineData& outData,
        std::string& outError)
    {
        outData.shader = std::make_shared<core::Shader>(device);

        material::MaterialRuntimeData runtimeData =
            material::MaterialRuntimeDataBuilder::fromMaterialData(materialData);
        outData.vertexShaderHash = runtimeData.shaderMap.vertexShaderHash;
        outData.fragmentShaderHash = runtimeData.shaderMap.fragmentShaderHash;
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
                outError = outData.shader->getLastCompilationError();
                vfLogError("Failed to compile material shader: {}", materialPath);
                return false;
            }
        }

        outError.clear();

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

    void MaterialShaderCache::destroyPipelineData(MaterialPipelineData& data)
    {
        auto logicalDevice = device.getLogicalDevice();
        if (data.opaquePipeline)
            logicalDevice.destroyPipeline(data.opaquePipeline);
        if (data.maskedPipeline)
            logicalDevice.destroyPipeline(data.maskedPipeline);
        if (data.translucentPipeline)
            logicalDevice.destroyPipeline(data.translucentPipeline);
        if (data.additivePipeline)
            logicalDevice.destroyPipeline(data.additivePipeline);
        if (data.multiplyPipeline)
            logicalDevice.destroyPipeline(data.multiplyPipeline);

        if (data.shader)
            data.shader->cleanUp();
    }

    void MaterialShaderCache::invalidateLocked(const std::string& materialPath)
    {
        auto it = cache.find(materialPath);
        if (it != cache.end())
        {
            device.getLogicalDevice().waitIdle();
            destroyPipelineData(it->second);
            cache.erase(it);
            vfLogInfo("Invalidated material shader cache: {}", materialPath);
        }
    }

    void MaterialShaderCache::invalidate(const std::string& materialPath)
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        invalidateLocked(materialPath);
    }

    void MaterialShaderCache::invalidateAll()
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (cache.empty()) return;

        device.getLogicalDevice().waitIdle();

        for (auto& [path, data] : cache)
        {
            destroyPipelineData(data);
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

    bool MaterialShaderCache::hasPipelineLocked(const std::string& materialPath) const
    {
        auto it = cache.find(materialPath);
        return it != cache.end() && it->second.valid;
    }

    bool MaterialShaderCache::hasPipeline(const std::string& materialPath) const
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        return hasPipelineLocked(materialPath);
    }

    std::string MaterialShaderCache::hashShaderSource(const std::string& source)
    {
        return material::MaterialRuntimeDataBuilder::shaderSourceHash(source);
    }
}
