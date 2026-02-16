#include "AudioSphereDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include <cmath>

namespace render::mesh
{
    AudioSphereDebugRenderer::AudioSphereDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    AudioSphereDebugRenderer::~AudioSphereDebugRenderer() = default;

    void AudioSphereDebugRenderer::init(vk::RenderPass renderPass)
    {
        loadShader();
        createPipeline(renderPass);
        createBuffers();
        initialized = true;
    }

    void AudioSphereDebugRenderer::recreate(vk::RenderPass renderPass)
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        createPipeline(renderPass);
    }

    void AudioSphereDebugRenderer::cleanUp()
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        destroyBufferPair(vertexBuffer, vertexBufferMemory);
        destroyBufferPair(indexBuffer, indexBufferMemory);
        initialized = false;
    }

    void AudioSphereDebugRenderer::cleanUpShader()
    {
        if (wireframeShader)
        {
            wireframeShader->cleanUp();
        }
    }

    void AudioSphereDebugRenderer::loadShader()
    {
        wireframeShader = std::make_shared<core::Shader>(device);
        wireframeShader->readShader("../../resources/shaders/tools/sphere_wireframe.glsl");
    }

    void AudioSphereDebugRenderer::createPipeline(vk::RenderPass renderPass)
    {
        core::WireframePipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .pushConstantSize = sizeof(AudioSpherePushConstants),
            .shaderStages = wireframeShader->getShaderStages()
        };

        auto result = core::PipelineUtilities::createWireframePipeline(config);
        wireframePipeline = result.pipeline;
        wireframePipelineLayout = result.pipelineLayout;
    }

    void AudioSphereDebugRenderer::createBuffers()
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        const float pi = 3.14159265358979323846f;
        const int segments = SPHERE_SEGMENTS;

        // Generate circle vertices for 3 planes
        // XY plane (horizontal ring at z=0)
        int baseIndex = 0;
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(std::cos(angle), std::sin(angle), 0.0f));
        }
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + (i + 1) % segments);
        }

        // XZ plane (vertical ring at y=0)
        baseIndex = static_cast<int>(vertices.size());
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(std::cos(angle), 0.0f, std::sin(angle)));
        }
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + (i + 1) % segments);
        }

        // YZ plane (vertical ring at x=0)
        baseIndex = static_cast<int>(vertices.size());
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(0.0f, std::cos(angle), std::sin(angle)));
        }
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + (i + 1) % segments);
        }

        indexCount = static_cast<uint32_t>(indices.size());

        // Create vertex buffer
        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, vertexBuffer, vertexBufferMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            vertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        // Create index buffer
        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, indexBuffer, indexBufferMemory);

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

    void AudioSphereDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                          const std::vector<AudioSphereRenderData>& audioSourceDrawList,
                                          const glm::mat4& view,
                                          const glm::mat4& projection) const
    {
        if (!initialized || !wireframePipeline || !vertexBuffer)
        {
            return;
        }

        bool hasSpheresToRender = false;
        for (const auto& audioSource : audioSourceDrawList)
        {
            if (audioSource.showDebugSpheres)
            {
                hasSpheresToRender = true;
                break;
            }
        }

        if (!hasSpheresToRender)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        glm::mat4 viewProj = projection * view;

        for (const auto& audioSource : audioSourceDrawList)
        {
            if (!audioSource.showDebugSpheres)
            {
                continue;
            }

            // Draw min distance sphere (green)
            {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), audioSource.position);
                model = glm::scale(model, glm::vec3(audioSource.minDistance));

                AudioSpherePushConstants pushConstants{};
                pushConstants.mvp = viewProj * model;
                pushConstants.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f); // Green

                commandBuffer.pushConstants(wireframePipelineLayout,
                                            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                            0, sizeof(AudioSpherePushConstants), &pushConstants);

                commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
            }

            // Draw max distance sphere (orange/yellow)
            {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), audioSource.position);
                model = glm::scale(model, glm::vec3(audioSource.maxDistance));

                AudioSpherePushConstants pushConstants{};
                pushConstants.mvp = viewProj * model;
                pushConstants.color = glm::vec4(1.0f, 0.6f, 0.0f, 1.0f); // Orange

                commandBuffer.pushConstants(wireframePipelineLayout,
                                            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                            0, sizeof(AudioSpherePushConstants), &pushConstants);

                commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
            }
        }
    }
}
