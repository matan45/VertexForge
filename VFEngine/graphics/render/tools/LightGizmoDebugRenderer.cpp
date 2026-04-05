#include "LightGizmoDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include <cmath>

namespace render::mesh
{
    LightGizmoDebugRenderer::LightGizmoDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    LightGizmoDebugRenderer::~LightGizmoDebugRenderer() = default;

    void LightGizmoDebugRenderer::init(vk::Format colorFormat, vk::Format depthFormat)
    {
        loadShader();
        createPipeline(colorFormat, depthFormat);
        createSphereBuffers();
        createConeBuffers();
        createArrowBuffers();
        initialized = true;
    }

    void LightGizmoDebugRenderer::recreate(vk::Format colorFormat, vk::Format depthFormat)
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        createPipeline(colorFormat, depthFormat);
    }

    void LightGizmoDebugRenderer::cleanUp()
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        destroyBufferPair(sphereVertexBuffer, sphereVertexBufferAllocation);
        destroyBufferPair(sphereIndexBuffer, sphereIndexBufferAllocation);
        destroyBufferPair(coneVertexBuffer, coneVertexBufferAllocation);
        destroyBufferPair(coneIndexBuffer, coneIndexBufferAllocation);
        destroyBufferPair(arrowVertexBuffer, arrowVertexBufferAllocation);
        destroyBufferPair(arrowIndexBuffer, arrowIndexBufferAllocation);
        initialized = false;
    }

    void LightGizmoDebugRenderer::cleanUpShader()
    {
        if (wireframeShader)
        {
            wireframeShader->cleanUp();
        }
    }

    void LightGizmoDebugRenderer::loadShader()
    {
        wireframeShader = std::make_shared<core::Shader>(device);
        wireframeShader->readShader("../../resources/shaders/tools/sphere_wireframe.glsl");
    }

    void LightGizmoDebugRenderer::createPipeline(vk::Format colorFormat, vk::Format depthFormat)
    {
        core::WireframePipelineConfig config{
            .device = device.getLogicalDevice(),
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat = depthFormat,
            .pushConstantSize = sizeof(LightGizmoPushConstants),
            .shaderStages = wireframeShader->getShaderStages()
        };

        auto result = core::PipelineUtilities::createWireframePipeline(config);
        wireframePipeline = result.pipeline;
        wireframePipelineLayout = result.pipelineLayout;
    }

    void LightGizmoDebugRenderer::createSphereBuffers()
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        const float pi = 3.14159265358979323846f;
        const int segments = SPHERE_SEGMENTS;

        // XY plane
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

        // XZ plane
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

        // YZ plane
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

    void LightGizmoDebugRenderer::createConeBuffers()
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        const float pi = 3.14159265358979323846f;
        const int segments = CONE_SEGMENTS;

        // Unit cone: tip at origin, base at z = -1, radius = 1
        vertices.push_back(glm::vec3(0.0f, 0.0f, 0.0f));
        uint32_t tipIndex = 0;

        uint32_t baseStartIndex = static_cast<uint32_t>(vertices.size());
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(std::cos(angle), std::sin(angle), -1.0f));
        }

        for (int i = 0; i < segments; i += segments / 4)
        {
            indices.push_back(tipIndex);
            indices.push_back(baseStartIndex + i);
        }

        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseStartIndex + i);
            indices.push_back(baseStartIndex + (i + 1) % segments);
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

    void LightGizmoDebugRenderer::createArrowBuffers()
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        // Unit arrow pointing in -Z direction
        float headSize = 0.15f;
        float crossSize = 0.2f;

        vertices.push_back(glm::vec3(0.0f, 0.0f, 0.0f));        // 0: Origin
        vertices.push_back(glm::vec3(0.0f, 0.0f, -1.0f));       // 1: Tip
        vertices.push_back(glm::vec3(headSize, 0.0f, -0.8f));   // 2: Arrow head
        vertices.push_back(glm::vec3(-headSize, 0.0f, -0.8f));  // 3
        vertices.push_back(glm::vec3(0.0f, headSize, -0.8f));   // 4
        vertices.push_back(glm::vec3(0.0f, -headSize, -0.8f));  // 5
        vertices.push_back(glm::vec3(crossSize, 0.0f, 0.0f));   // 6: Cross at origin
        vertices.push_back(glm::vec3(-crossSize, 0.0f, 0.0f));  // 7
        vertices.push_back(glm::vec3(0.0f, crossSize, 0.0f));   // 8
        vertices.push_back(glm::vec3(0.0f, -crossSize, 0.0f));  // 9

        indices.push_back(0); indices.push_back(1);
        indices.push_back(1); indices.push_back(2);
        indices.push_back(1); indices.push_back(3);
        indices.push_back(1); indices.push_back(4);
        indices.push_back(1); indices.push_back(5);
        indices.push_back(6); indices.push_back(7);
        indices.push_back(8); indices.push_back(9);

        arrowIndexCount = static_cast<uint32_t>(indices.size());

        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, arrowVertexBuffer, arrowVertexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            arrowVertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, arrowIndexBuffer, arrowIndexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            arrowIndexBuffer,
            indices.data(),
            indexBufferSize
        );
    }

    void LightGizmoDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                          const std::vector<LightGizmoRenderData>& lightGizmoDrawList,
                                          const glm::mat4& view,
                                          const glm::mat4& projection) const
    {
        if (!initialized || !wireframePipeline || lightGizmoDrawList.empty())
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);

        glm::mat4 viewProj = projection * view;

        for (const auto& light : lightGizmoDrawList)
        {
            switch (light.type)
            {
            case LightGizmoType::Point:
                renderPointLight(commandBuffer, light, viewProj);
                break;
            case LightGizmoType::Spot:
                renderSpotLight(commandBuffer, light, viewProj);
                break;
            case LightGizmoType::Directional:
                renderDirectionalLight(commandBuffer, light, viewProj);
                break;
            }
        }
    }

    void LightGizmoDebugRenderer::renderPointLight(const vk::CommandBuffer& commandBuffer,
                                                    const LightGizmoRenderData& light,
                                                    const glm::mat4& viewProj) const
    {
        if (!sphereVertexBuffer)
        {
            return;
        }

        commandBuffer.bindIndexBuffer(sphereIndexBuffer, 0, vk::IndexType::eUint32);

        vk::Buffer vertexBuffers[] = {sphereVertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        glm::vec3 position = glm::vec3(light.worldMatrix[3]);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
        model = glm::scale(model, glm::vec3(light.radius));

        LightGizmoPushConstants pushConstants{};
        pushConstants.mvp = viewProj * model;
        pushConstants.color = glm::vec4(light.color, 0.8f);

        commandBuffer.pushConstants(wireframePipelineLayout,
                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(LightGizmoPushConstants), &pushConstants);

        commandBuffer.drawIndexed(sphereIndexCount, 1, 0, 0, 0);
    }

    void LightGizmoDebugRenderer::renderSpotLight(const vk::CommandBuffer& commandBuffer,
                                                   const LightGizmoRenderData& light,
                                                   const glm::mat4& viewProj) const
    {
        if (!coneVertexBuffer)
        {
            return;
        }

        commandBuffer.bindIndexBuffer(coneIndexBuffer, 0, vk::IndexType::eUint32);

        vk::Buffer vertexBuffers[] = {coneVertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        float outerRadians = glm::radians(light.outerAngle);
        float outerRadius = std::tan(outerRadians) * light.range;

        glm::mat4 model = light.worldMatrix;
        model = glm::scale(model, glm::vec3(outerRadius, outerRadius, light.range));

        LightGizmoPushConstants pushConstants{};
        pushConstants.mvp = viewProj * model;
        pushConstants.color = glm::vec4(light.color, 0.8f);

        commandBuffer.pushConstants(wireframePipelineLayout,
                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(LightGizmoPushConstants), &pushConstants);

        commandBuffer.drawIndexed(coneIndexCount, 1, 0, 0, 0);

        if (light.innerAngle > 0.0f && light.innerAngle < light.outerAngle)
        {
            float innerRadians = glm::radians(light.innerAngle);
            float innerRadius = std::tan(innerRadians) * light.range;

            glm::mat4 innerModel = light.worldMatrix;
            innerModel = glm::scale(innerModel, glm::vec3(innerRadius, innerRadius, light.range));

            pushConstants.mvp = viewProj * innerModel;
            pushConstants.color = glm::vec4(light.color, 0.5f);

            commandBuffer.pushConstants(wireframePipelineLayout,
                                        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                        0, sizeof(LightGizmoPushConstants), &pushConstants);

            commandBuffer.drawIndexed(coneIndexCount, 1, 0, 0, 0);
        }
    }

    void LightGizmoDebugRenderer::renderDirectionalLight(const vk::CommandBuffer& commandBuffer,
                                                          const LightGizmoRenderData& light,
                                                          const glm::mat4& viewProj) const
    {
        if (!arrowVertexBuffer)
        {
            return;
        }

        commandBuffer.bindIndexBuffer(arrowIndexBuffer, 0, vk::IndexType::eUint32);

        vk::Buffer vertexBuffers[] = {arrowVertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        glm::mat4 model = light.worldMatrix;
        model = glm::scale(model, glm::vec3(ARROW_LENGTH));

        LightGizmoPushConstants pushConstants{};
        pushConstants.mvp = viewProj * model;
        pushConstants.color = glm::vec4(light.color, 0.8f);

        commandBuffer.pushConstants(wireframePipelineLayout,
                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(LightGizmoPushConstants), &pushConstants);

        commandBuffer.drawIndexed(arrowIndexCount, 1, 0, 0, 0);
    }
}
