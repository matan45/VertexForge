#include "DebugRenderer.hpp"
#include "tools/AABBDebugRenderer.hpp"
#include "tools/FrustumDebugRenderer.hpp"
#include "tools/AudioSphereDebugRenderer.hpp"
#include "tools/GridRenderer.hpp"

namespace render
{
    DebugRenderer::DebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
        aabbRenderer = std::make_unique<mesh::AABBDebugRenderer>(device, swapChain);
        frustumRenderer = std::make_unique<mesh::FrustumDebugRenderer>(device, swapChain);
        audioSphereRenderer = std::make_unique<mesh::AudioSphereDebugRenderer>(device, swapChain);
        gridRenderer = std::make_unique<mesh::GridRenderer>(device, swapChain);
    }

    DebugRenderer::~DebugRenderer() = default;

    void DebugRenderer::init(vk::RenderPass renderPass)
    {
        aabbRenderer->init(renderPass);
        frustumRenderer->init(renderPass);
        audioSphereRenderer->init(renderPass);
        gridRenderer->init(renderPass);
        initialized = true;
    }

    void DebugRenderer::recreate(vk::RenderPass renderPass)
    {
        aabbRenderer->recreate(renderPass);
        frustumRenderer->recreate(renderPass);
        audioSphereRenderer->recreate(renderPass);
        gridRenderer->recreate(renderPass);
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
        if (audioSphereRenderer)
        {
            audioSphereRenderer->cleanUp();
        }
        if (gridRenderer)
        {
            gridRenderer->cleanUp();
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
        if (audioSphereRenderer)
        {
            audioSphereRenderer->cleanUpShader();
        }
        if (gridRenderer)
        {
            gridRenderer->cleanUpShader();
        }
    }

    void DebugRenderer::setCameraFrustumDrawList(std::vector<mesh::CameraFrustumRenderData>&& frustums)
    {
        cameraFrustumDrawList = std::move(frustums);
    }

    void DebugRenderer::setAudioSphereDrawList(std::vector<mesh::AudioSphereRenderData>&& spheres)
    {
        audioSphereDrawList = std::move(spheres);
    }

    void DebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                               const std::vector<mesh::MeshRenderData>& meshDrawList,
                               const glm::mat4& view,
                               const glm::mat4& projection,
                               const std::function<const mesh::MeshGPUData*(const std::string&)>& getMeshFunc) const
    {
        if (gridRenderer && showGrid)
        {
            gridRenderer->render(commandBuffer, view, projection);
        }
        
        if (aabbRenderer)
        {
            aabbRenderer->render(commandBuffer, meshDrawList, view, projection, getMeshFunc);
        }
        
        if (frustumRenderer && !cameraFrustumDrawList.empty())
        {
            frustumRenderer->render(commandBuffer, cameraFrustumDrawList, view, projection);
        }
        
        if (audioSphereRenderer && !audioSphereDrawList.empty())
        {
            audioSphereRenderer->render(commandBuffer, audioSphereDrawList, view, projection);
        }
    }

    bool DebugRenderer::hasItemsToRender() const
    {
        return showGrid || !cameraFrustumDrawList.empty() || !audioSphereDrawList.empty() ||
            hasBoundingBoxesToRender;
    }

    void DebugRenderer::setShowGrid(bool show)
    {
        showGrid = show;
        if (gridRenderer)
        {
            gridRenderer->setVisible(show);
        }
    }
}
