#include "RuntimeRenderServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/PostProcessEvents.hpp"
#include "../../events/render/AtmosphereEvents.hpp"
#include "DayNightSync.hpp"
#include "print/Log.hpp"
#include <filesystem>
#include <utility>

namespace services
{
    RuntimeRenderServiceImpl::RuntimeRenderServiceImpl(IOffScreenProvider* offScreenProvider,
                                                       IPostProcessProvider* postProcessProvider)
        : offScreenProvider(offScreenProvider)
          , postProcessProvider(postProcessProvider)
    {
    }

    ViewportTextureHandle RuntimeRenderServiceImpl::getViewportTexture()
    {
        if (!offScreenProvider)
        {
            return ViewportTextureHandle{};
        }

        frameCounter++;
        prepareCameras();
        prepareSceneData();
        prepareFrameUIImages();

        void* descriptorSet = preOffscreenRenderCallback
            ? offScreenProvider->render(preOffscreenRenderCallback)
            : offScreenProvider->render();

        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = descriptorSet;
        handle.width = viewportWidth;
        handle.height = viewportHeight;

        return handle;
    }

    void RuntimeRenderServiceImpl::resizeViewport(uint32_t width, uint32_t height)
    {
        if (width == viewportWidth && height == viewportHeight)
        {
            return;
        }

        viewportWidth = width;
        viewportHeight = height;
    }

    void RuntimeRenderServiceImpl::getViewportSize(uint32_t& width, uint32_t& height) const
    {
        width = viewportWidth;
        height = viewportHeight;
    }

    bool RuntimeRenderServiceImpl::setIBL(const std::string& hdrPath)
    {
        if (!offScreenProvider)
        {
            return false;
        }

        if (!std::filesystem::exists(hdrPath))
        {
            vfLogError("IBL file not found: {}", hdrPath);
            return false;
        }

        if (currentIBLPath.has_value())
        {
            offScreenProvider->iblRemove();
        }

        offScreenProvider->iblSet(hdrPath);
        currentIBLPath = hdrPath;

        return true;
    }

    void RuntimeRenderServiceImpl::updateIBLCamera(const glm::mat4& view, const glm::mat4& projection)
    {
        if (!offScreenProvider || !currentIBLPath.has_value())
        {
            return;
        }
        offScreenProvider->iblSetCameraMatrices(view, projection);
    }

    void RuntimeRenderServiceImpl::removeIBL()
    {
        if (!offScreenProvider)
        {
            return;
        }

        offScreenProvider->iblRemove();
        currentIBLPath = std::nullopt;
    }

    bool RuntimeRenderServiceImpl::hasIBL() const
    {
        return currentIBLPath.has_value();
    }

    std::optional<std::string> RuntimeRenderServiceImpl::getIBLPath() const
    {
        return currentIBLPath;
    }

    bool RuntimeRenderServiceImpl::isReady() const
    {
        return offScreenProvider != nullptr;
    }

    uint64_t RuntimeRenderServiceImpl::getFrameNumber() const
    {
        return frameCounter;
    }

    void RuntimeRenderServiceImpl::setPreOffscreenRenderCallback(std::function<void()> callback)
    {
        preOffscreenRenderCallback = std::move(callback);
    }

    void RuntimeRenderServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::render::SetIBLCommand>(
            [this](const events::render::SetIBLCommand& cmd)
            {
                return setIBL(cmd.hdrPath);
            });

        dispatcher.registerCommandHandler<events::render::RemoveIBLCommand>(
            [this](const events::render::RemoveIBLCommand&)
            {
                removeIBL();
            });

        dispatcher.registerCommandHandler<events::render::UpdateIBLCameraCommand>(
            [this](const events::render::UpdateIBLCameraCommand& cmd)
            {
                updateIBLCamera(cmd.viewMatrix, cmd.projectionMatrix);
            });

        dispatcher.registerQueryHandler<events::render::GetViewportTextureQuery>(
            [this](const events::render::GetViewportTextureQuery&)
            {
                return getViewportTexture();
            });

        dispatcher.registerCommandHandler<events::render::UpdateMeshCameraCommand>(
            [this](const events::render::UpdateMeshCameraCommand& cmd)
            {
                updateMeshCamera(cmd.viewMatrix, cmd.projectionMatrix, cmd.cameraPosition, cmd.time);
            });

        dispatcher.registerQueryHandler<events::render::GetMeshBoundingBoxQuery>(
            [this](const events::render::GetMeshBoundingBoxQuery& q)
            {
                return getMeshBoundingBox(q.meshPath);
            });

        dispatcher.registerCommandHandler<events::render::ApplyShadowSettingsCommand>(
            [this](const events::render::ApplyShadowSettingsCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->applyShadowSettings(cmd.settings);
                }
            });

        dispatcher.registerCommandHandler<events::postprocess::ApplyPostProcessSettingsCommand>(
            [this](const events::postprocess::ApplyPostProcessSettingsCommand& cmd)
            {
                if (postProcessProvider)
                {
                    postProcessProvider->applyPostProcessSettings(cmd.settings);
                }
            });

        dispatcher.registerQueryHandler<events::postprocess::GetPostProcessSettingsQuery>(
            [this](const events::postprocess::GetPostProcessSettingsQuery&)
            {
                return postProcessProvider
                           ? postProcessProvider->getPostProcessSettings()
                           : postprocess::PostProcessSettings{};
            });

        dispatcher.registerCommandHandler<events::postprocess::SetPostProcessEnabledCommand>(
            [this](const events::postprocess::SetPostProcessEnabledCommand& cmd)
            {
                if (postProcessProvider)
                {
                    postProcessProvider->setPostProcessEnabled(cmd.enabled);
                }
            });

        dispatcher.registerQueryHandler<events::postprocess::GetPostProcessEnabledQuery>(
            [this](const events::postprocess::GetPostProcessEnabledQuery&)
            {
                return postProcessProvider ? postProcessProvider->isPostProcessEnabled() : true;
            });

        // VK-1566 / runtime parity: the editor registers these in EditorRenderServiceImpl, but
        // runtime had no atmosphere handlers, so a scene's atmosphere + day-night cycle never
        // reached the runtime pipeline. Register the apply/query here (no editor-only auto-"Sun"
        // creation) plus the per-frame day-night advance dispatched by the SunSync frame step.
        dispatcher.registerCommandHandler<events::atmosphere::ApplyAtmosphereSettingsCommand>(
            [this](const events::atmosphere::ApplyAtmosphereSettingsCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->applyAtmosphereSettings(cmd.settings);
            });

        dispatcher.registerQueryHandler<events::atmosphere::GetAtmosphereSettingsQuery>(
            [this](const events::atmosphere::GetAtmosphereSettingsQuery&)
            {
                return offScreenProvider
                           ? offScreenProvider->getAtmosphereSettings()
                           : render::atmosphere::AtmosphereSettings{};
            });

        dispatcher.registerCommandHandler<events::atmosphere::UpdateDayNightCommand>(
            [this](const events::atmosphere::UpdateDayNightCommand& cmd)
            {
                tickDayNightAndSyncSun(offScreenProvider, cmd.deltaTime);
            });

        meshDataChangedToken = dispatcher.subscribe<events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification)
            {
                if (!notification.meshPath.empty() && !isMeshLoaded(notification.meshPath))
                {
                    loadMesh(notification.meshPath);
                }
            });
    }

    std::string RuntimeRenderServiceImpl::loadMesh(const std::string& meshPath)
    {
        if (!offScreenProvider)
        {
            return "";
        }
        return offScreenProvider->meshLoad(meshPath);
    }

    void RuntimeRenderServiceImpl::updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                                                    const glm::vec3& cameraPos, float time)
    {
        if (offScreenProvider)
        {
            offScreenProvider->meshUpdateCamera(MAIN_CAMERA_ID, view, projection, cameraPos, time);
        }
    }

    bool RuntimeRenderServiceImpl::isMeshLoaded(const std::string& meshPath) const
    {
        return offScreenProvider && offScreenProvider->isMeshLoaded(meshPath);
    }

    std::optional<MeshBoundingBox> RuntimeRenderServiceImpl::getMeshBoundingBox(const std::string& meshPath) const
    {
        if (!offScreenProvider)
        {
            return std::nullopt;
        }
        auto bounds = offScreenProvider->getMeshBoundingBox(meshPath);
        if (!bounds)
        {
            return std::nullopt;
        }
        MeshBoundingBox result;
        result.min = bounds->min;
        result.max = bounds->max;
        return result;
    }

    void RuntimeRenderServiceImpl::prepareCameras()
    {
        if (offScreenProvider)
        {
            offScreenProvider->prepareCameras();
        }
    }

    void RuntimeRenderServiceImpl::prepareFrameMeshes()
    {
        if (offScreenProvider)
        {
            offScreenProvider->prepareFrameMeshes();
        }
    }

    void RuntimeRenderServiceImpl::prepareFrameText()
    {
        if (offScreenProvider)
        {
            offScreenProvider->prepareFrameText();
        }
    }

    void RuntimeRenderServiceImpl::prepareSceneData()
    {
        if (offScreenProvider)
        {
            offScreenProvider->prepareSceneData();
        }
    }

    void RuntimeRenderServiceImpl::prepareFrameUIImages()
    {
        if (offScreenProvider)
        {
            offScreenProvider->prepareFrameUIImages();
        }
    }
}
