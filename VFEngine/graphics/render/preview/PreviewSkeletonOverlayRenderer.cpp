#include "PreviewSkeletonOverlayRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../tools/ImmediateDebugRenderer.hpp" // ImmediateDebugPushConstants (mat4 viewProj)

#include <array>
#include <cstring>

namespace render::preview
{
    PreviewSkeletonOverlayRenderer::PreviewSkeletonOverlayRenderer(core::Device& device, core::SwapChain& swapChain,
                                                                   core::OffscreenResources& offscreenResources)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
    {
    }

    PreviewSkeletonOverlayRenderer::~PreviewSkeletonOverlayRenderer() = default;

    void PreviewSkeletonOverlayRenderer::init()
    {
        loadShader();
        createPipeline();

        const size_t imageCount = offscreenResources.colorImages.size();
        vertexBuffers.assign(imageCount, nullptr);
        vertexAllocations.assign(imageCount, core::VulkanAllocation{});
        vertexCounts.assign(imageCount, 0);
        bufferSizes.assign(imageCount, 0);

        initialized = true;
    }

    void PreviewSkeletonOverlayRenderer::cleanUp()
    {
        if (!initialized) return;

        if (pipeline || pipelineLayout)
        {
            device.getLogicalDevice().destroyPipeline(pipeline);
            device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
            pipeline = nullptr;
            pipelineLayout = nullptr;
        }

        for (size_t i = 0; i < vertexBuffers.size(); ++i)
        {
            destroyBuffer(i);
        }
        vertexBuffers.clear();
        vertexAllocations.clear();
        vertexCounts.clear();
        bufferSizes.clear();

        initialized = false;
    }

    void PreviewSkeletonOverlayRenderer::cleanUpShader()
    {
        if (shader)
        {
            shader->cleanUp();
        }
    }

    void PreviewSkeletonOverlayRenderer::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/tools/immediate_debug.glsl");
    }

    void PreviewSkeletonOverlayRenderer::createPipeline()
    {
        vk::VertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(render::mesh::DebugLineVertex);
        binding.inputRate = vk::VertexInputRate::eVertex;

        std::array<vk::VertexInputAttributeDescription, 2> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = vk::Format::eR32G32B32Sfloat;
        attributes[0].offset = offsetof(render::mesh::DebugLineVertex, position);
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
        attributes[1].offset = offsetof(render::mesh::DebugLineVertex, color);

        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.colorAttachmentFormats = {swapChain.getSceneColorFormat()};
        config.depthAttachmentFormat = swapChain.getSwapchainDepthStencilFormat();
        config.extent = swapChain.getSwapchainExtent();
        config.shaderStages = shader->getShaderStages();
        config.vertexBindings = {binding};
        config.vertexAttributes = {attributes[0], attributes[1]};
        config.topology = vk::PrimitiveTopology::eLineList;
        config.pushConstantSize = sizeof(render::mesh::ImmediateDebugPushConstants);
        config.pushConstantStages = vk::ShaderStageFlagBits::eVertex;
        config.cullMode = vk::CullModeFlagBits::eNone;
        // Depth test OFF: the rig validation overlay must read THROUGH the mesh.
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.blendEnable = true;

        config.dynamicSampleCount = true;
        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        pipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void PreviewSkeletonOverlayRenderer::destroyBuffer(size_t imageIndex)
    {
        if (vertexBuffers[imageIndex])
        {
            device.getLogicalDevice().destroyBuffer(vertexBuffers[imageIndex]);
            vertexBuffers[imageIndex] = nullptr;
        }
        if (vertexAllocations[imageIndex])
        {
            device.getMemoryManager().free(vertexAllocations[imageIndex]);
            vertexAllocations[imageIndex] = core::VulkanAllocation{};
        }
        bufferSizes[imageIndex] = 0;
        vertexCounts[imageIndex] = 0;
    }

    void PreviewSkeletonOverlayRenderer::uploadLines(uint32_t imageIndex,
                                                     const render::mesh::ImmediateDebugDrawList& lines)
    {
        const uint32_t count = static_cast<uint32_t>(lines.vertexCount());
        if (count == 0)
        {
            vertexCounts[imageIndex] = 0;
            return;
        }

        const vk::DeviceSize bufferSize =
            static_cast<vk::DeviceSize>(count) * sizeof(render::mesh::DebugLineVertex);

        if (bufferSize > bufferSizes[imageIndex])
        {
            // destroyBuffer() also zeroes vertexCounts/bufferSizes for this slot; the count is
            // (re)assigned below after the copy, so the final value reflects this frame's lines.
            destroyBuffer(imageIndex);

            const vk::DeviceSize allocSize = bufferSize + bufferSize / 2; // grow with headroom

            // Host-visible + persistently mapped: the per-frame line set is tiny and rewritten
            // every overlay-on frame. A device-local buffer would need copyToBuffer(), which
            // submits a one-time command buffer + queue.waitIdle() under the graphics-queue mutex
            // — and render() is called BETWEEN the controller's commandBuffer.begin()/.end(), so
            // every overlay-on frame would stall the graphics queue. Mapping lets us memcpy with
            // no submit, mirroring the engine's per-frame UBO pattern (PerFrameBuffer). The
            // allocator auto-populates mappedPtr for host-visible memory.
            core::BufferInfoRequest request(device.getLogicalDevice(), device.getPhysicalDevice());
            request.size = allocSize;
            request.usage = vk::BufferUsageFlagBits::eVertexBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, vertexBuffers[imageIndex], vertexAllocations[imageIndex],
                                                device.getMemoryManager());

            bufferSizes[imageIndex] = allocSize;
        }

        // Same-frame memcpy into mapped device memory — data is present before the draw records
        // below, with no GPU submit/stall. Indexing by imageIndex keeps the host write for image N
        // off image N-1's still-in-flight buffer.
        if (void* dst = vertexAllocations[imageIndex].mappedPtr)
        {
            std::memcpy(dst, lines.lineVertices.data(), static_cast<size_t>(bufferSize));
        }

        vertexCounts[imageIndex] = count;
    }

    void PreviewSkeletonOverlayRenderer::render(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                                const glm::mat4& view, const glm::mat4& projection,
                                                const render::mesh::ImmediateDebugDrawList& lines)
    {
        if (!initialized || imageIndex >= vertexBuffers.size())
            return;

        uploadLines(imageIndex, lines);
        if (vertexCounts[imageIndex] == 0 || !pipeline || !vertexBuffers[imageIndex])
            return;

        vk::Image colorImage = offscreenResources.colorImages[imageIndex].colorImage;
        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = offscreenResources.depthImage.depthImageView;

        core::ImageUtilities::transitionImageLayout(commandBuffer, colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::DynamicRenderingInfo renderingInfo{};
        renderingInfo.extent = swapChain.getSwapchainExtent();
        renderingInfo.colorAttachments = {core::colorLoad(colorView)};
        renderingInfo.depthAttachment = core::depthLoad(depthView);

        core::beginDynamicRendering(commandBuffer, renderingInfo);
        // Match the (single-sample) preview target's sample count, like PreviewGridRenderer.
        commandBuffer.setRasterizationSamplesEXT(offscreenResources.sampleCount);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        vk::Buffer buffers[] = {vertexBuffers[imageIndex]};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);

        render::mesh::ImmediateDebugPushConstants pushConstants{};
        pushConstants.viewProj = projection * view;
        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex,
                                    0, sizeof(render::mesh::ImmediateDebugPushConstants), &pushConstants);

        commandBuffer.draw(vertexCounts[imageIndex], 1, 0, 0);

        core::endDynamicRendering(commandBuffer);

        core::ImageUtilities::transitionImageLayout(commandBuffer, colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }
}
