#include "MaterialShaderCache.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
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
        if (!initialized) {
            loggerWarning("MaterialShaderCache not initialized");
            return nullptr;
        }

        // Check if material has cached shaders
        if (materialData.cachedVertexShader.empty() || materialData.cachedFragmentShader.empty()) {
            return nullptr;  // No custom shader, use default pipeline
        }

        // Check if already cached
        auto it = cache.find(materialPath);
        if (it != cache.end()) {
            // Check if shader source changed
            std::string vsHash = hashShaderSource(materialData.cachedVertexShader);
            std::string fsHash = hashShaderSource(materialData.cachedFragmentShader);

            if (it->second.vertexShaderHash == vsHash &&
                it->second.fragmentShaderHash == fsHash &&
                it->second.valid) {
                return &it->second;
            }

            // Shader changed, invalidate and recompile
            invalidate(materialPath);
        }

        // Create new pipeline
        MaterialPipelineData data;
        if (compileAndCreatePipeline(materialPath, materialData, data)) {
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
        // Create shader object
        outData.shader = std::make_shared<core::Shader>(device);

        // Compile from source
        bool success = outData.shader->compileFromSources(
            materialData.cachedVertexShader,
            materialData.cachedFragmentShader,
            materialData.name
        );

        if (!success || outData.shader->getShaderStages().empty()) {
            lastCompilationError = outData.shader->getLastCompilationError();
            loggerError("Failed to compile material shader: {}", materialPath);
            return false;
        }

        // Clear error on success
        lastCompilationError.clear();

        // Store hashes for change detection
        outData.vertexShaderHash = hashShaderSource(materialData.cachedVertexShader);
        outData.fragmentShaderHash = hashShaderSource(materialData.cachedFragmentShader);

        // Create pipelines
        if (!createPipelines(outData)) {
            loggerError("Failed to create pipeline for material: {}", materialPath);
            return false;
        }

        outData.valid = true;
        loggerInfo("Compiled and cached material shader: {}", materialPath);
        return true;
    }

    bool MaterialShaderCache::createPipelines(MaterialPipelineData& data)
    {
        // Vertex input state - using MeshVertexInput helper
        auto bindingDescription = MeshVertexInput::getBindingDescription();
        auto attributeDescriptions = MeshVertexInput::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        // Input assembly
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport and scissor
        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchainExtent.width);
        viewport.height = static_cast<float>(swapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D(0, 0);
        scissor.extent = swapchainExtent;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        // Rasterizer
        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        // Multisampling
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Depth testing
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Color blending - opaque
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                              vk::ColorComponentFlagBits::eG |
                                              vk::ColorComponentFlagBits::eB |
                                              vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // Create opaque pipeline
        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(data.shader->getShaderStages().size());
        pipelineInfo.pStages = data.shader->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        auto result = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            loggerError("Failed to create opaque pipeline");
            return false;
        }
        data.opaquePipeline = result.value;

        // Masked pipeline (same as opaque)
        result = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            loggerError("Failed to create masked pipeline");
            return false;
        }
        data.maskedPipeline = result.value;

        // Translucent pipeline with alpha blending
        vk::PipelineColorBlendAttachmentState translucentBlendAttachment{};
        translucentBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                                    vk::ColorComponentFlagBits::eG |
                                                    vk::ColorComponentFlagBits::eB |
                                                    vk::ColorComponentFlagBits::eA;
        translucentBlendAttachment.blendEnable = VK_TRUE;
        translucentBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        translucentBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        translucentBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        translucentBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        translucentBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        translucentBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

        vk::PipelineColorBlendStateCreateInfo translucentBlending{};
        translucentBlending.logicOpEnable = VK_FALSE;
        translucentBlending.attachmentCount = 1;
        translucentBlending.pAttachments = &translucentBlendAttachment;

        vk::PipelineDepthStencilStateCreateInfo translucentDepthStencil{};
        translucentDepthStencil.depthTestEnable = VK_TRUE;
        translucentDepthStencil.depthWriteEnable = VK_FALSE;
        translucentDepthStencil.depthCompareOp = vk::CompareOp::eLess;
        translucentDepthStencil.depthBoundsTestEnable = VK_FALSE;
        translucentDepthStencil.stencilTestEnable = VK_FALSE;

        pipelineInfo.pDepthStencilState = &translucentDepthStencil;
        pipelineInfo.pColorBlendState = &translucentBlending;

        result = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            loggerError("Failed to create translucent pipeline");
            return false;
        }
        data.translucentPipeline = result.value;

        return true;
    }

    void MaterialShaderCache::invalidate(const std::string& materialPath)
    {
        auto it = cache.find(materialPath);
        if (it != cache.end()) {
            device.getLogicalDevice().waitIdle();

            if (it->second.opaquePipeline)
                device.getLogicalDevice().destroyPipeline(it->second.opaquePipeline);
            if (it->second.maskedPipeline)
                device.getLogicalDevice().destroyPipeline(it->second.maskedPipeline);
            if (it->second.translucentPipeline)
                device.getLogicalDevice().destroyPipeline(it->second.translucentPipeline);

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

        for (auto& [path, data] : cache) {
            if (data.opaquePipeline)
                device.getLogicalDevice().destroyPipeline(data.opaquePipeline);
            if (data.maskedPipeline)
                device.getLogicalDevice().destroyPipeline(data.maskedPipeline);
            if (data.translucentPipeline)
                device.getLogicalDevice().destroyPipeline(data.translucentPipeline);

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
