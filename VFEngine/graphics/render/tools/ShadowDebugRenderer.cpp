#include "ShadowDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include <cmath>

namespace render::mesh
{
    ShadowDebugRenderer::ShadowDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    ShadowDebugRenderer::~ShadowDebugRenderer() = default;

    void ShadowDebugRenderer::init(vk::Format colorFormat, vk::Format depthFormat)
    {
        loadShaders();
        createPipelines(colorFormat, depthFormat);
        createFrustumBuffers();
        createSphereBuffers();
        initialized = true;
    }

    void ShadowDebugRenderer::recreate(vk::Format colorFormat, vk::Format depthFormat)
    {
        destroyPipelineAndLayout(frustumPipeline, frustumPipelineLayout);
        destroyPipelineAndLayout(spherePipeline, spherePipelineLayout);
        createPipelines(colorFormat, depthFormat);
    }

    void ShadowDebugRenderer::cleanUp()
    {
        destroyPipelineAndLayout(frustumPipeline, frustumPipelineLayout);
        destroyPipelineAndLayout(spherePipeline, spherePipelineLayout);
        destroyBufferPair(frustumVertexBuffer, frustumVertexBufferAllocation);
        destroyBufferPair(frustumIndexBuffer, frustumIndexBufferAllocation);
        destroyBufferPair(sphereVertexBuffer, sphereVertexBufferAllocation);
        destroyBufferPair(sphereIndexBuffer, sphereIndexBufferAllocation);
        initialized = false;
    }

    void ShadowDebugRenderer::cleanUpShader()
    {
        if (frustumShader)
        {
            frustumShader->cleanUp();
        }
        if (sphereShader)
        {
            sphereShader->cleanUp();
        }
    }

    void ShadowDebugRenderer::loadShaders()
    {
        // Use the same wireframe shader as FrustumDebugRenderer for frustums
        frustumShader = std::make_shared<core::Shader>(device);
        frustumShader->readShader("../../resources/shaders/tools/wireframe.glsl");

        // Use the same sphere wireframe shader as LightGizmoDebugRenderer for spheres
        sphereShader = std::make_shared<core::Shader>(device);
        sphereShader->readShader("../../resources/shaders/tools/sphere_wireframe.glsl");
    }

    void ShadowDebugRenderer::createPipelines(vk::Format colorFormat, vk::Format depthFormat)
    {
        {
            core::WireframePipelineConfig config{
                .device = device.getLogicalDevice(),
                .extent = swapChain.getSwapchainExtent(),
                .colorAttachmentFormats = {colorFormat},
                .depthAttachmentFormat = depthFormat,
                .pushConstantSize = sizeof(ShadowDebugPushConstants),
                .shaderStages = frustumShader->getShaderStages()
            };

            auto result = core::PipelineUtilities::createWireframePipeline(config);
            frustumPipeline = result.pipeline;
            frustumPipelineLayout = result.pipelineLayout;
        }

        {
            struct SpherePushConstants
            {
                glm::mat4 mvp;
                glm::vec4 color;
            };

            core::WireframePipelineConfig config{
                .device = device.getLogicalDevice(),
                .extent = swapChain.getSwapchainExtent(),
                .colorAttachmentFormats = {colorFormat},
                .depthAttachmentFormat = depthFormat,
                .pushConstantSize = sizeof(SpherePushConstants),
                .shaderStages = sphereShader->getShaderStages()
            };

            auto result = core::PipelineUtilities::createWireframePipeline(config);
            spherePipeline = result.pipeline;
            spherePipelineLayout = result.pipelineLayout;
        }
    }

    void ShadowDebugRenderer::createFrustumBuffers()
    {
        vk::DeviceSize vertexBufferSize = sizeof(ndcCorners);
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, frustumVertexBuffer, frustumVertexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            frustumVertexBuffer,
            ndcCorners.data(),
            vertexBufferSize
        );

        vk::DeviceSize indexBufferSize = sizeof(frustumIndices);
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, frustumIndexBuffer, frustumIndexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            frustumIndexBuffer,
            frustumIndices.data(),
            indexBufferSize
        );
    }

    void ShadowDebugRenderer::createSphereBuffers()
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        const float pi = 3.14159265358979323846f;
        const int segments = SPHERE_SEGMENTS;

        // XY plane circle
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

        // XZ plane circle
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

        // YZ plane circle
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

        sphereIndexCount = static_cast<uint32_t>(indices.size());

        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, sphereVertexBuffer, sphereVertexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            sphereVertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, sphereIndexBuffer, sphereIndexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            sphereIndexBuffer,
            indices.data(),
            indexBufferSize
        );
    }

    glm::vec4 ShadowDebugRenderer::getColorForShadow(ShadowFrustumType type, uint32_t cascadeIndex)
    {
        switch (type)
        {
            case ShadowFrustumType::DirectionalCascade:
                return cascadeColors[std::min(cascadeIndex, 3u)];
            case ShadowFrustumType::SpotFrustum:
                return spotLightColor;
            case ShadowFrustumType::PointSphere:
                return pointLightColor;
            default:
                return {1.0f, 1.0f, 1.0f, 0.8f};
        }
    }

    void ShadowDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                      const std::vector<ShadowFrustumRenderData>& shadowDrawList,
                                      const glm::mat4& editorView,
                                      const glm::mat4& editorProjection) const
    {
        if (!initialized || shadowDrawList.empty())
        {
            return;
        }

        glm::mat4 editorViewProj = editorProjection * editorView;

        bool hasFrustums = false;
        for (const auto& shadow : shadowDrawList)
        {
            if (shadow.type == ShadowFrustumType::DirectionalCascade ||
                shadow.type == ShadowFrustumType::SpotFrustum)
            {
                hasFrustums = true;
                break;
            }
        }

        if (hasFrustums && frustumPipeline && frustumVertexBuffer)
        {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, frustumPipeline);
            commandBuffer.bindIndexBuffer(frustumIndexBuffer, 0, vk::IndexType::eUint32);

            vk::Buffer vertexBuffers[] = {frustumVertexBuffer};
            vk::DeviceSize offsets[] = {0};
            commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

            for (const auto& shadow : shadowDrawList)
            {
                if (shadow.type == ShadowFrustumType::DirectionalCascade ||
                    shadow.type == ShadowFrustumType::SpotFrustum)
                {
                    renderFrustum(commandBuffer, shadow, editorViewProj);
                }
            }
        }

        bool hasSpheres = false;
        for (const auto& shadow : shadowDrawList)
        {
            if (shadow.type == ShadowFrustumType::PointSphere)
            {
                hasSpheres = true;
                break;
            }
        }

        if (hasSpheres && spherePipeline && sphereVertexBuffer)
        {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, spherePipeline);
            commandBuffer.bindIndexBuffer(sphereIndexBuffer, 0, vk::IndexType::eUint32);

            vk::Buffer vertexBuffers[] = {sphereVertexBuffer};
            vk::DeviceSize offsets[] = {0};
            commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

            for (const auto& shadow : shadowDrawList)
            {
                if (shadow.type == ShadowFrustumType::PointSphere)
                {
                    renderSphere(commandBuffer, shadow, editorViewProj);
                }
            }
        }
    }

    void ShadowDebugRenderer::renderFrustum(const vk::CommandBuffer& commandBuffer,
                                             const ShadowFrustumRenderData& shadow,
                                             const glm::mat4& editorViewProj) const
    {
        ShadowDebugPushConstants pushConstants{};
        pushConstants.viewProj = editorViewProj;
        pushConstants.inverseViewProj = shadow.inverseViewProjection;
        pushConstants.color = getColorForShadow(shadow.type, shadow.cascadeIndex);

        commandBuffer.pushConstants(frustumPipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(ShadowDebugPushConstants), &pushConstants);

        commandBuffer.drawIndexed(24, 1, 0, 0, 0);
    }

    void ShadowDebugRenderer::renderSphere(const vk::CommandBuffer& commandBuffer,
                                            const ShadowFrustumRenderData& shadow,
                                            const glm::mat4& editorViewProj) const
    {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), shadow.lightPosition);
        model = glm::scale(model, glm::vec3(shadow.radius));

        struct SpherePushConstants
        {
            glm::mat4 mvp;
            glm::vec4 color;
        } pushConstants;

        pushConstants.mvp = editorViewProj * model;
        pushConstants.color = getColorForShadow(shadow.type, shadow.cascadeIndex);

        commandBuffer.pushConstants(spherePipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(SpherePushConstants), &pushConstants);

        commandBuffer.drawIndexed(sphereIndexCount, 1, 0, 0, 0);
    }
}
