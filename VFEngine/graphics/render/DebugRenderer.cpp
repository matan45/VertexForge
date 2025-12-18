#include "DebugRenderer.hpp"
#include "mesh/AABBDebugRenderer.hpp"
#include "mesh/FrustumDebugRenderer.hpp"

namespace render
{
    DebugRenderer::DebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
        aabbRenderer = std::make_unique<mesh::AABBDebugRenderer>(device, swapChain);
        frustumRenderer = std::make_unique<mesh::FrustumDebugRenderer>(device, swapChain);
    }

    DebugRenderer::~DebugRenderer() = default;

    void DebugRenderer::init(vk::RenderPass renderPass)
    {
        aabbRenderer->init(renderPass);
        frustumRenderer->init(renderPass);
        initialized = true;
    }

    void DebugRenderer::recreate(vk::RenderPass renderPass)
    {
        aabbRenderer->recreate(renderPass);
        frustumRenderer->recreate(renderPass);
    }

    void DebugRenderer::cleanUp()
    {
        if (aabbRenderer)
        {
            aabbRenderer->cleanUp();
        }
        if (frustumRenderer)
        {
            frustumRenderer->cleanUp();
        }
        initialized = false;
    }

    void DebugRenderer::cleanUpShaders()
    {
        if (aabbRenderer)
        {
            aabbRenderer->cleanUpShader();
        }
        if (frustumRenderer)
        {
            frustumRenderer->cleanUpShader();
        }
    }

    void DebugRenderer::setCameraFrustumDrawList(std::vector<mesh::CameraFrustumRenderData>&& frustums)
    {
        cameraFrustumDrawList = std::move(frustums);
    }

    void DebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                const std::vector<mesh::MeshRenderData>& meshDrawList,
                                const glm::mat4& view,
                                const glm::mat4& projection,
                                const std::function<const mesh::MeshGPUData*(const std::string&)>& getMeshFunc) const
    {
        // Render AABB wireframes for meshes with showBoundingBox enabled
        if (aabbRenderer)
        {
            aabbRenderer->render(commandBuffer, meshDrawList, view, projection, getMeshFunc);
        }

        // Render camera frustum wireframes for cameras with showFrustum enabled
        if (frustumRenderer && !cameraFrustumDrawList.empty())
        {
            frustumRenderer->render(commandBuffer, cameraFrustumDrawList, view, projection);
        }
    }
}
