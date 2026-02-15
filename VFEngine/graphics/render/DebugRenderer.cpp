#include "DebugRenderer.hpp"
#include "tools/AABBDebugRenderer.hpp"
#include "tools/FrustumDebugRenderer.hpp"
#include "tools/AudioSphereDebugRenderer.hpp"
#include "tools/GridRenderer.hpp"
#include "tools/PhysicsDebugRenderer.hpp"
#include "tools/LightGizmoDebugRenderer.hpp"
#include "tools/ClusterDebugRenderer.hpp"
#include "tools/ShadowDebugRenderer.hpp"
#include "tools/UICanvasDebugRenderer.hpp"
#include "tools/UICanvasImageRenderer.hpp"

namespace render
{
    DebugRenderer::DebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
        aabbRenderer = std::make_unique<mesh::AABBDebugRenderer>(device, swapChain);
        frustumRenderer = std::make_unique<mesh::FrustumDebugRenderer>(device, swapChain);
        audioSphereRenderer = std::make_unique<mesh::AudioSphereDebugRenderer>(device, swapChain);
        gridRenderer = std::make_unique<mesh::GridRenderer>(device, swapChain);
        physicsDebugRenderer = std::make_unique<mesh::PhysicsDebugRenderer>(device, swapChain);
        lightGizmoRenderer = std::make_unique<mesh::LightGizmoDebugRenderer>(device, swapChain);
        clusterDebugRenderer = std::make_unique<mesh::ClusterDebugRenderer>(device, swapChain);
        shadowDebugRenderer = std::make_unique<mesh::ShadowDebugRenderer>(device, swapChain);
        uiCanvasRenderer = std::make_unique<mesh::UICanvasDebugRenderer>(device, swapChain);
        uiCanvasImageRenderer = std::make_unique<mesh::UICanvasImageRenderer>(device, swapChain);
    }

    DebugRenderer::~DebugRenderer() = default;

    void DebugRenderer::init(vk::RenderPass renderPass)
    {
        aabbRenderer->init(renderPass);
        frustumRenderer->init(renderPass);
        audioSphereRenderer->init(renderPass);
        gridRenderer->init(renderPass);
        physicsDebugRenderer->init(renderPass);
        lightGizmoRenderer->init(renderPass);
        clusterDebugRenderer->init(renderPass);
        shadowDebugRenderer->init(renderPass);
        uiCanvasRenderer->init(renderPass);
        uiCanvasImageRenderer->init(renderPass);
        initialized = true;
    }

    void DebugRenderer::recreate(vk::RenderPass renderPass)
    {
        aabbRenderer->recreate(renderPass);
        frustumRenderer->recreate(renderPass);
        audioSphereRenderer->recreate(renderPass);
        gridRenderer->recreate(renderPass);
        physicsDebugRenderer->recreate(renderPass);
        lightGizmoRenderer->recreate(renderPass);
        clusterDebugRenderer->recreate(renderPass);
        shadowDebugRenderer->recreate(renderPass);
        uiCanvasRenderer->recreate(renderPass);
        uiCanvasImageRenderer->recreate(renderPass);
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
        if (physicsDebugRenderer)
        {
            physicsDebugRenderer->cleanUp();
        }
        if (lightGizmoRenderer)
        {
            lightGizmoRenderer->cleanUp();
        }
        if (clusterDebugRenderer)
        {
            clusterDebugRenderer->cleanUp();
        }
        if (shadowDebugRenderer)
        {
            shadowDebugRenderer->cleanUp();
        }
        if (uiCanvasRenderer)
        {
            uiCanvasRenderer->cleanUp();
        }
        if (uiCanvasImageRenderer)
        {
            uiCanvasImageRenderer->cleanUp();
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
        if (physicsDebugRenderer)
        {
            physicsDebugRenderer->cleanUpShader();
        }
        if (lightGizmoRenderer)
        {
            lightGizmoRenderer->cleanUpShader();
        }
        if (clusterDebugRenderer)
        {
            clusterDebugRenderer->cleanUpShader();
        }
        if (shadowDebugRenderer)
        {
            shadowDebugRenderer->cleanUpShader();
        }
        if (uiCanvasRenderer)
        {
            uiCanvasRenderer->cleanUpShader();
        }
        if (uiCanvasImageRenderer)
        {
            uiCanvasImageRenderer->cleanUpShader();
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

    void DebugRenderer::setPhysicsColliderDrawList(std::vector<mesh::PhysicsColliderRenderData>&& colliders)
    {
        physicsColliderDrawList = std::move(colliders);
    }

    void DebugRenderer::setLightGizmoDrawList(std::vector<mesh::LightGizmoRenderData>&& gizmos)
    {
        lightGizmoDrawList = std::move(gizmos);
    }

    void DebugRenderer::setClusterDebugData(mesh::ClusterDebugRenderData&& data)
    {
        clusterDebugData = std::make_unique<mesh::ClusterDebugRenderData>(std::move(data));
    }

    void DebugRenderer::setShowClusterDebug(bool show)
    {
        showClusterDebug = show;
        if (clusterDebugRenderer)
        {
            clusterDebugRenderer->setVisible(show);
        }
    }

    void DebugRenderer::setShadowFrustumDrawList(std::vector<mesh::ShadowFrustumRenderData>&& frustums)
    {
        shadowFrustumDrawList = std::move(frustums);
    }

    void DebugRenderer::setUICanvasOutlineDrawList(std::vector<mesh::UICanvasOutlineRenderData>&& outlines)
    {
        uiCanvasDrawList = std::move(outlines);
    }

    void DebugRenderer::setUICanvasImageDrawList(std::vector<mesh::UICanvasImageRenderData>&& images)
    {
        uiCanvasImageDrawList = std::move(images);
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

        if (physicsDebugRenderer && showPhysicsDebug && !physicsColliderDrawList.empty())
        {
            physicsDebugRenderer->render(commandBuffer, physicsColliderDrawList, view, projection);
        }

        if (lightGizmoRenderer && !lightGizmoDrawList.empty())
        {
            lightGizmoRenderer->render(commandBuffer, lightGizmoDrawList, view, projection);
        }

        if (clusterDebugRenderer && showClusterDebug && clusterDebugData)
        {
            clusterDebugRenderer->render(commandBuffer, *clusterDebugData, view, projection);
        }

        if (shadowDebugRenderer && showShadowDebug && !shadowFrustumDrawList.empty())
        {
            shadowDebugRenderer->render(commandBuffer, shadowFrustumDrawList, view, projection);
        }

        if (uiCanvasRenderer && !uiCanvasDrawList.empty())
        {
            uiCanvasRenderer->render(commandBuffer, uiCanvasDrawList, view, projection);
        }

        if (uiCanvasImageRenderer && !uiCanvasImageDrawList.empty())
        {
            uiCanvasImageRenderer->render(commandBuffer, uiCanvasImageDrawList, view, projection);
        }
    }

    bool DebugRenderer::hasItemsToRender() const
    {
        return showGrid || !cameraFrustumDrawList.empty() || !audioSphereDrawList.empty() ||
            hasBoundingBoxesToRender || (showPhysicsDebug && !physicsColliderDrawList.empty()) ||
            !lightGizmoDrawList.empty() || (showClusterDebug && clusterDebugData) ||
            (showShadowDebug && !shadowFrustumDrawList.empty()) ||
            !uiCanvasDrawList.empty() || !uiCanvasImageDrawList.empty();
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
