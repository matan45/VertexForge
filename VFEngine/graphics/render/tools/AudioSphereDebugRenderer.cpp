#include "AudioSphereDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace render::mesh
{
    AudioSphereDebugRenderer::AudioSphereDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    AudioSphereDebugRenderer::~AudioSphereDebugRenderer() = default;

    void AudioSphereDebugRenderer::init(vk::Format colorFormat, vk::Format depthFormat)
    {
        loadShader();
        createPipeline(colorFormat, depthFormat);
        createBuffers();
        createConeBuffers();
        initialized = true;
    }

    void AudioSphereDebugRenderer::recreate(vk::Format colorFormat, vk::Format depthFormat)
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        createPipeline(colorFormat, depthFormat);
    }

    void AudioSphereDebugRenderer::cleanUp()
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        destroyBufferPair(vertexBuffer, vertexBufferAllocation);
        destroyBufferPair(indexBuffer, indexBufferAllocation);
        destroyBufferPair(coneVertexBuffer, coneVertexBufferAllocation);
        destroyBufferPair(coneIndexBuffer, coneIndexBufferAllocation);
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

    void AudioSphereDebugRenderer::createPipeline(vk::Format colorFormat, vk::Format depthFormat)
    {
        core::WireframePipelineConfig config{
            .device = device.getLogicalDevice(),
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat = depthFormat,
            .pushConstantSize = sizeof(AudioSpherePushConstants),
            .shaderStages = wireframeShader->getShaderStages()
        };

        config.dynamicSampleCount = true;
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

        // Create index buffer
        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
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

    void AudioSphereDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                          const std::vector<AudioSphereRenderData>& audioSourceDrawList,
                                          const glm::mat4& view,
                                          const glm::mat4& projection) const
    {
        if (!initialized || !wireframePipeline || !vertexBuffer)
        {
            return;
        }

        bool hasAnythingToRender = false;
        for (const auto& audioSource : audioSourceDrawList)
        {
            if (audioSource.showDebugSpheres ||
                (audioSource.showDebugCone && audioSource.innerConeAngle < 360.0f))
            {
                hasAnythingToRender = true;
                break;
            }
        }

        if (!hasAnythingToRender)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);

        glm::mat4 viewProj = projection * view;

        // Render spheres
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

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

        renderCones(commandBuffer, audioSourceDrawList, viewProj);
    }

    void AudioSphereDebugRenderer::createConeBuffers()
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        const float pi = 3.14159265358979323846f;
        const int segments = CONE_SEGMENTS;

        // Vertex 0: apex (origin)
        vertices.push_back(glm::vec3(0.0f, 0.0f, 0.0f));

        // Vertices 1..segments: base circle at z = -1 (unit cone along -Z)
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(std::cos(angle), std::sin(angle), -1.0f));
        }

        // Lines from apex to base vertices
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(0);
            indices.push_back(1 + i);
        }

        // Base circle lines
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(1 + i);
            indices.push_back(1 + (i + 1) % segments);
        }

        coneIndexCount = static_cast<uint32_t>(indices.size());

        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, coneVertexBuffer, coneVertexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            coneVertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, coneIndexBuffer, coneIndexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            coneIndexBuffer,
            indices.data(),
            indexBufferSize
        );
    }

    void AudioSphereDebugRenderer::renderCones(const vk::CommandBuffer& commandBuffer,
                                                const std::vector<AudioSphereRenderData>& audioSourceDrawList,
                                                const glm::mat4& viewProj) const
    {
        if (!coneVertexBuffer || coneIndexCount == 0)
        {
            return;
        }

        bool hasConesToRender = false;
        for (const auto& audioSource : audioSourceDrawList)
        {
            if (audioSource.showDebugCone && audioSource.innerConeAngle < 360.0f)
            {
                hasConesToRender = true;
                break;
            }
        }

        if (!hasConesToRender)
        {
            return;
        }

        vk::Buffer coneVertexBuffers[] = {coneVertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, coneVertexBuffers, offsets);
        commandBuffer.bindIndexBuffer(coneIndexBuffer, 0, vk::IndexType::eUint32);

        const float pi = 3.14159265358979323846f;

        for (const auto& audioSource : audioSourceDrawList)
        {
            if (!audioSource.showDebugCone || audioSource.innerConeAngle >= 360.0f)
            {
                continue;
            }

            // Compute rotation to align -Z with direction
            glm::vec3 dir = glm::normalize(audioSource.direction);
            glm::vec3 defaultDir(0.0f, 0.0f, -1.0f);
            glm::mat4 rotation(1.0f);

            float dot = glm::dot(defaultDir, dir);
            if (dot < -0.9999f)
            {
                rotation = glm::rotate(glm::mat4(1.0f), pi, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            else if (dot < 0.9999f)
            {
                glm::vec3 axis = glm::normalize(glm::cross(defaultDir, dir));
                float angle = std::acos(glm::clamp(dot, -1.0f, 1.0f));
                rotation = glm::rotate(glm::mat4(1.0f), angle, axis);
            }

            auto drawCone = [&](float coneAngleDeg, float length, const glm::vec4& color)
            {
                float halfAngleRad = glm::radians(coneAngleDeg * 0.5f);
                float baseRadius = length * std::tan(halfAngleRad);

                glm::mat4 model = glm::translate(glm::mat4(1.0f), audioSource.position);
                model = model * rotation;
                model = glm::scale(model, glm::vec3(baseRadius, baseRadius, length));

                AudioSpherePushConstants pushConstants{};
                pushConstants.mvp = viewProj * model;
                pushConstants.color = color;

                commandBuffer.pushConstants(wireframePipelineLayout,
                                            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                            0, sizeof(AudioSpherePushConstants), &pushConstants);

                commandBuffer.drawIndexed(coneIndexCount, 1, 0, 0, 0);
            };

            // Inner cone (cyan)
            drawCone(audioSource.innerConeAngle, audioSource.minDistance,
                     glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));

            // Outer cone (yellow)
            if (audioSource.outerConeAngle > audioSource.innerConeAngle &&
                audioSource.outerConeAngle < 360.0f)
            {
                drawCone(audioSource.outerConeAngle, audioSource.minDistance,
                         glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
            }
        }
    }
}
