#include "UICanvasImageRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"
#include <filesystem>

namespace render::mesh
{
    UICanvasImageRenderer::UICanvasImageRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    UICanvasImageRenderer::~UICanvasImageRenderer() = default;

    void UICanvasImageRenderer::init(vk::Format colorFormat, vk::Format depthFormat)
    {
        loadShader();
        createDescriptorSetLayout();
        createDescriptorPool();
        createPipeline(colorFormat, depthFormat);
        createBuffers();
        initialized = true;
    }

    void UICanvasImageRenderer::recreate(vk::Format colorFormat, vk::Format depthFormat)
    {
        destroyPipelineAndLayout(graphicsPipeline, pipelineLayout);
        createPipeline(colorFormat, depthFormat);
    }

    void UICanvasImageRenderer::cleanUp()
    {
        destroyPipelineAndLayout(graphicsPipeline, pipelineLayout);

        textureCache.clear();

        auto& dev = device.getLogicalDevice();
        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        destroyBufferPair(vertexBuffer, vertexBufferAllocation);
        destroyBufferPair(indexBuffer, indexBufferAllocation);
        initialized = false;
    }

    void UICanvasImageRenderer::cleanUpShader()
    {
        if (shader)
        {
            shader->cleanUp();
        }
    }

    void UICanvasImageRenderer::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/tools/ui_canvas_image.glsl");
    }

    void UICanvasImageRenderer::createDescriptorSetLayout()
    {
        vk::DescriptorSetLayoutBinding samplerBinding{};
        samplerBinding.binding = 0;
        samplerBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        samplerBinding.descriptorCount = 1;
        samplerBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        samplerBinding.pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &samplerBinding;

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void UICanvasImageRenderer::createDescriptorPool()
    {
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = MAX_TEXTURES;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = MAX_TEXTURES;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void UICanvasImageRenderer::createPipeline(vk::Format colorFormat, vk::Format depthFormat)
    {
        // Vertex binding: position (vec2) + texcoord (vec2)
        vk::VertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(Vertex);
        bindingDesc.inputRate = vk::VertexInputRate::eVertex;

        std::vector<vk::VertexInputAttributeDescription> attribDescs(2);
        attribDescs[0].binding = 0;
        attribDescs[0].location = 0;
        attribDescs[0].format = vk::Format::eR32G32Sfloat;
        attribDescs[0].offset = offsetof(Vertex, position);

        attribDescs[1].binding = 0;
        attribDescs[1].location = 1;
        attribDescs[1].format = vk::Format::eR32G32Sfloat;
        attribDescs[1].offset = offsetof(Vertex, texCoord);

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat = depthFormat,
            .shaderStages = shader->getShaderStages(),
            .vertexBindings = {bindingDesc},
            .vertexAttributes = std::move(attribDescs),
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {descriptorSetLayout},
            .pushConstantSize = sizeof(UICanvasImagePushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = true,
            .depthWriteEnable = false,
            .blendEnable = true
        };

        config.dynamicSampleCount = true;
        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void UICanvasImageRenderer::createBuffers()
    {
        // Vertex buffer
        vk::DeviceSize vertexBufferSize = sizeof(vertices);
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, vertexBuffer, vertexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            vertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        // Index buffer
        vk::DeviceSize indexBufferSize = sizeof(indices);
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, indexBuffer, indexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            indexBuffer,
            indices.data(),
            indexBufferSize
        );
    }

    void UICanvasImageRenderer::updateDescriptorSet(vk::DescriptorSet dstSet,
                                                      vk::ImageView imageView, vk::Sampler sampler)
    {
        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;

        vk::WriteDescriptorSet imageWrite{};
        imageWrite.dstSet = dstSet;
        imageWrite.dstBinding = 0;
        imageWrite.dstArrayElement = 0;
        imageWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        imageWrite.descriptorCount = 1;
        imageWrite.pImageInfo = &imageInfo;

        device.getLogicalDevice().updateDescriptorSets(imageWrite, nullptr);
    }

    bool UICanvasImageRenderer::loadTexture(const std::string& texturePath)
    {
        if (textureCache.contains(texturePath))
        {
            return true;
        }

        if (textureCache.size() >= MAX_TEXTURES)
        {
            vfLogWarning("UI canvas image texture limit reached ({}), cannot load: {}",
                          MAX_TEXTURES, texturePath);
            return false;
        }

        if (!std::filesystem::exists(texturePath))
        {
            vfLogWarning("UI canvas image texture file not found: {}", texturePath);
            return false;
        }

        auto texture = std::make_unique<core::Texture>(device);
        texture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Unorm, false);

        if (!texture->getImageView())
        {
            vfLogError("Failed to load UI canvas image texture: {}", texturePath);
            return false;
        }

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        vk::DescriptorSet newDescSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
        updateDescriptorSet(newDescSet, texture->getImageView(), texture->getSampler());

        TextureEntry entry;
        entry.texture = std::move(texture);
        entry.descriptorSet = newDescSet;
        textureCache.emplace(texturePath, std::move(entry));

        return true;
    }

    void UICanvasImageRenderer::render(const vk::CommandBuffer& commandBuffer,
                                        const std::vector<UICanvasImageRenderData>& imageDrawList,
                                        const glm::mat4& editorView,
                                        const glm::mat4& editorProjection) const
    {
        if (!initialized || !graphicsPipeline || !vertexBuffer || imageDrawList.empty())
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        glm::mat4 editorViewProj = editorProjection * editorView;

        for (const auto& image : imageDrawList)
        {
            auto it = textureCache.find(image.texturePath);
            if (it == textureCache.end())
            {
                // Try to load texture (const_cast needed since render is const but loadTexture mutates cache)
                auto* mutableThis = const_cast<UICanvasImageRenderer*>(this);
                if (!mutableThis->loadTexture(image.texturePath))
                {
                    continue;
                }
                it = textureCache.find(image.texturePath);
                if (it == textureCache.end())
                {
                    continue;
                }
            }

            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                              0, it->second.descriptorSet, nullptr);

            UICanvasImagePushConstants pushConstants{};
            pushConstants.viewProj = editorViewProj;
            pushConstants.modelMatrix = image.modelMatrix;
            pushConstants.colorTint = image.colorTint;
            pushConstants.uvRect = image.uvRect;

            commandBuffer.pushConstants(pipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(UICanvasImagePushConstants), &pushConstants);

            commandBuffer.drawIndexed(6, 1, 0, 0, 0);
            render::FrameDrawStats::count();
        }
    }
}
