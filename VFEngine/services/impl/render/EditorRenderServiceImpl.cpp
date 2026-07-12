#include "print/Log.hpp"
#include "EditorRenderServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/render/PostProcessEvents.hpp"
#include "../../events/render/PostProcessEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include <exception>
#include <filesystem>

namespace services
{
    EditorRenderServiceImpl::EditorRenderServiceImpl(IOffScreenProvider* offScreenProvider,
                                                     IEditorTextureProvider* textureProvider,
                                                     IPostProcessProvider* postProcessProvider)
        : offScreenProvider(offScreenProvider)
          , textureProvider(textureProvider)
          , postProcessProvider(postProcessProvider)
    {
    }

    EditorRenderServiceImpl::~EditorRenderServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (meshDataChangedToken.isValid())
        {
            dispatcher.unsubscribe(meshDataChangedToken);
        }

        if (editorModePreChangeToken.isValid())
        {
            dispatcher.unsubscribe(editorModePreChangeToken);
        }

        if (editorModeChangedToken.isValid())
        {
            dispatcher.unsubscribe(editorModeChangedToken);
        }

        if (navmeshBakeCompleteToken.isValid())
        {
            dispatcher.unsubscribe(navmeshBakeCompleteToken);
        }
    }

    ViewportTextureHandle EditorRenderServiceImpl::getViewportTexture()
    {
        if (!offScreenProvider)
        {
            return ViewportTextureHandle{};
        }

        // Skip render preparation during scene transitions to prevent
        // race condition with entity destruction (registry is not thread-safe).
        // consumeTransitionSkip() decrements an internal counter each call and
        // auto-clears the flag when it expires, so even if this code path isn't
        // reached (e.g. viewport hidden), the flag won't stay stuck forever.
        if (scene::EntityRegistry::consumeTransitionSkip())
        {
            return lastViewportHandle;
        }

        frameCounter++;

        // Prepare CPU-side data (main thread)
        offScreenProvider->prepareGrid();
        offScreenProvider->prepareCameras();

        offScreenProvider->prepareSceneData();

        offScreenProvider->prepareFrameCameraFrustums();
        offScreenProvider->prepareFrameAudioSpheres();
        offScreenProvider->prepareFrameLightGizmos();
        offScreenProvider->prepareFramePhysicsColliders();
        offScreenProvider->prepareFrameClusterDebug();
        offScreenProvider->prepareFrameShadowDebug();
        offScreenProvider->prepareFrameUICanvasOutlines();
        offScreenProvider->prepareFrameUIImages();

        // VK-1490: push the current editor selection for the silhouette outline
        // passes. Polling per frame makes clear-on-deselect/delete/scene-replace
        // automatic; play mode pushes empty so the runtime frame is untouched.
        {
            auto& dispatcher = events::EventDispatcher::instance();
            std::vector<EntityHandle> outlineSelection;
            if (!dispatcher.query(events::editor::IsPlayModeQuery{}))
            {
                outlineSelection = dispatcher.query(events::scene::GetSelectedEntitiesQuery{});
            }
            offScreenProvider->prepareFrameSelectionOutline(outlineSelection);
        }

        // Mark that preparation is done; GPU render will happen on the render thread
        viewportPrepared = true;

        // Return PREVIOUS frame's result (1-frame latency)
        // The render thread will update lastViewportHandle after rendering
        return lastViewportHandle;
    }

    void EditorRenderServiceImpl::renderViewportDeferred()
    {
        renderViewportDeferred({});
    }

    void EditorRenderServiceImpl::renderViewportDeferred(const std::function<void()>& preRenderCallback)
    {
        if (!viewportPrepared || !offScreenProvider)
            return;

        viewportPrepared = false;

        void* descriptorSet = preRenderCallback
            ? offScreenProvider->render(preRenderCallback)
            : offScreenProvider->render();

        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = descriptorSet;
        handle.width = viewportWidth;
        handle.height = viewportHeight;

        lastViewportHandle = handle;
    }

    void EditorRenderServiceImpl::resizeViewport(uint32_t width, uint32_t height)
    {
        if (width == viewportWidth && height == viewportHeight)
        {
            return;
        }

        viewportWidth = width;
        viewportHeight = height;

        // Clear stale descriptor set - old offscreen resources will be destroyed during recreate
        lastViewportHandle = ViewportTextureHandle{};
    }

    void EditorRenderServiceImpl::getViewportSize(uint32_t& width, uint32_t& height) const
    {
        width = viewportWidth;
        height = viewportHeight;
    }

    bool EditorRenderServiceImpl::setIBL(const std::string& hdrPath)
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

    void EditorRenderServiceImpl::updateIBLCamera(const glm::mat4& view, const glm::mat4& projection)
    {
        if (!offScreenProvider || !currentIBLPath.has_value())
        {
            return;
        }
        offScreenProvider->iblSetCameraMatrices(view, projection);
    }

    void EditorRenderServiceImpl::removeIBL()
    {
        if (!offScreenProvider)
        {
            return;
        }

        offScreenProvider->iblRemove();
        currentIBLPath = std::nullopt;
    }

    bool EditorRenderServiceImpl::hasIBL() const
    {
        return currentIBLPath.has_value();
    }

    std::optional<std::string> EditorRenderServiceImpl::getIBLPath() const
    {
        return currentIBLPath;
    }

    EditorTextureHandle EditorRenderServiceImpl::loadEditorTexture(const std::string& path)
    {
        if (!textureProvider)
        {
            return EditorTextureHandle{};
        }

        auto textureData = textureProvider->loadTexture(path);

        if (!textureData.valid)
        {
            return EditorTextureHandle{};
        }

        EditorTextureHandle handle;
        handle.imguiDescriptorSet = textureData.descriptorSet;
        handle.width = static_cast<uint32_t>(textureData.width);
        handle.height = static_cast<uint32_t>(textureData.height);
        handle.mipLevels = static_cast<uint32_t>(textureData.mipLevels);
        handle.mipDescriptorSets = textureData.mipDescriptorSets;

        loadedTextures[handle.imguiDescriptorSet] = handle;

        return handle;
    }

    EditorTextureHandle EditorRenderServiceImpl::loadEditorTextureFromData(resource::TextureData&& textureData)
    {
        if (!textureProvider)
        {
            return EditorTextureHandle{};
        }

        auto result = textureProvider->loadTextureFromData(std::move(textureData));

        if (!result.valid)
        {
            return EditorTextureHandle{};
        }

        EditorTextureHandle handle;
        handle.imguiDescriptorSet = result.descriptorSet;
        handle.width = static_cast<uint32_t>(result.width);
        handle.height = static_cast<uint32_t>(result.height);
        handle.mipLevels = static_cast<uint32_t>(result.mipLevels);
        handle.mipDescriptorSets = result.mipDescriptorSets;

        loadedTextures[handle.imguiDescriptorSet] = handle;

        return handle;
    }

    void EditorRenderServiceImpl::releaseEditorTexture(const EditorTextureHandle& handle)
    {
        if (textureProvider && loadedTextures.count(handle.imguiDescriptorSet))
        {
            textureProvider->releaseTexture(handle.imguiDescriptorSet);
            loadedTextures.erase(handle.imguiDescriptorSet);
        }
    }

    bool EditorRenderServiceImpl::isReady() const
    {
        return offScreenProvider != nullptr;
    }

    uint64_t EditorRenderServiceImpl::getFrameNumber() const
    {
        return frameCounter;
    }

    void EditorRenderServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        registerIBLHandlers(dispatcher);
        registerTextureHandlers(dispatcher);
        registerViewportHandlers(dispatcher);
        registerCullingHandlers(dispatcher);
        registerTerrainRenderHandlers(dispatcher);
        registerPostProcessHandlers(dispatcher);
        registerAtmosphereHandlers(dispatcher);
        registerCloudHandlers(dispatcher);
        registerWeatherHandlers(dispatcher);

        meshDataChangedToken = dispatcher.subscribe<events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification)
            {
                if (!notification.meshPath.empty() && !isMeshLoaded(notification.meshPath))
                {
                    loadMesh(notification.meshPath);
                }
            });

        editorModePreChangeToken = dispatcher.subscribe<events::editor::EditorModePreChangeNotification>(
            [this](const events::editor::EditorModePreChangeNotification& notification)
            {
                if (notification.previousMode == services::EditorMode::Play &&
                    notification.currentMode == services::EditorMode::Edit)
                {
                    waitForOffScreenIdleDuringPlayModeStop();
                }
            });

        editorModeChangedToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& notification)
            {
                if (offScreenProvider)
                {
                    bool isPlayMode = notification.currentMode == services::EditorMode::Play;
                    offScreenProvider->setPlayMode(isPlayMode);
                }
            });

        navmeshBakeCompleteToken = dispatcher.subscribe<events::navmesh::NavmeshBakeCompleteNotification>(
            [this](const events::navmesh::NavmeshBakeCompleteNotification& notification)
            {
                // Refresh the debug overlay when a navmesh finishes baking or loading
                // while "Show Navmesh" is already enabled (e.g. scene auto-load).
                if (!notification.success || !showNavmeshDebug || !offScreenProvider)
                {
                    return;
                }

                auto debugMesh = events::EventDispatcher::instance().query(
                    events::navmesh::GetNavmeshDebugMeshQuery{});
                if (!debugMesh.vertices.empty() && !debugMesh.indices.empty())
                {
                    offScreenProvider->updateNavmeshDebugMesh(debugMesh.vertices, debugMesh.indices);
                }
            });
    }

    void EditorRenderServiceImpl::registerIBLHandlers(events::EventDispatcher& dispatcher)
    {
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

        dispatcher.registerCommandHandler<events::render::RemoveCameraCommand>(
            [this](const events::render::RemoveCameraCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->removeCamera(cmd.cameraId);
                }
            });

        dispatcher.registerCommandHandler<events::render::UpdateIBLCameraCommand>(
            [this](const events::render::UpdateIBLCameraCommand& cmd)
            {
                updateIBLCamera(cmd.viewMatrix, cmd.projectionMatrix);
            });
    }

    void EditorRenderServiceImpl::registerTextureHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::render::LoadEditorTextureCommand>(
            [this](const events::render::LoadEditorTextureCommand& cmd)
            {
                return loadEditorTexture(cmd.path);
            });

        dispatcher.registerCommandHandler<events::render::ReleaseEditorTextureCommand>(
            [this](const events::render::ReleaseEditorTextureCommand& cmd)
            {
                EditorTextureHandle handle;
                handle.imguiDescriptorSet = cmd.handle;
                releaseEditorTexture(handle);
            });

        dispatcher.registerCommandHandler<events::render::LoadEditorTextureFromDataCommand>(
            [this](const events::render::LoadEditorTextureFromDataCommand& cmd)
            {
                return loadEditorTextureFromData(std::move(cmd.textureData));
            });

        dispatcher.registerCommandHandler<events::render::LoadEditorTextureAsyncCommand>(
            [this](const events::render::LoadEditorTextureAsyncCommand& cmd)
            {
                if (textureProvider)
                {
                    textureProvider->loadTextureAsync(cmd.instanceId, cmd.path, cmd.isHDR);
                }
            });

        dispatcher.registerCommandHandler<events::render::CancelTextureLoadingCommand>(
            [this](const events::render::CancelTextureLoadingCommand& cmd)
            {
                if (textureProvider)
                {
                    textureProvider->cancelTextureLoading(cmd.instanceId);
                }
            });

        dispatcher.registerQueryHandler<events::render::GetTextureLoadingProgressQuery>(
            [this](const events::render::GetTextureLoadingProgressQuery& query)
            {
                if (textureProvider)
                {
                    return textureProvider->getTextureLoadingProgress(query.instanceId);
                }
                return TextureLoadingProgress{};
            });

        dispatcher.registerQueryHandler<events::render::GetLoadedTextureHandleQuery>(
            [this](const events::render::GetLoadedTextureHandleQuery& query)
            {
                EditorTextureHandle result;
                if (!textureProvider)
                {
                    return result;
                }

                auto textureData = textureProvider->getLoadedTexture(query.instanceId);
                if (!textureData.valid)
                {
                    return result;
                }

                result.imguiDescriptorSet = textureData.descriptorSet;
                result.width = static_cast<uint32_t>(textureData.width);
                result.height = static_cast<uint32_t>(textureData.height);
                result.mipLevels = static_cast<uint32_t>(textureData.mipLevels);
                result.mipDescriptorSets = textureData.mipDescriptorSets;

                loadedTextures[result.imguiDescriptorSet] = result;

                return result;
            });
    }

    void EditorRenderServiceImpl::registerPostProcessHandlers(events::EventDispatcher& dispatcher)
    {
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

        dispatcher.registerQueryHandler<events::postprocess::GetUpscaleStatusQuery>(
            [this](const events::postprocess::GetUpscaleStatusQuery&)
            {
                return postProcessProvider
                    ? postProcessProvider->getUpscaleStatus()
                    : services::UpscaleStatus{};
            });
    }

    std::string EditorRenderServiceImpl::loadMesh(const std::string& meshPath)
    {
        if (!offScreenProvider)
        {
            return "";
        }
        return offScreenProvider->meshLoad(meshPath);
    }

    void EditorRenderServiceImpl::updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                                                   const glm::vec3& cameraPos, float time)
    {
        if (offScreenProvider)
        {
            offScreenProvider->meshUpdateCamera(MAIN_CAMERA_ID, view, projection, cameraPos, time);
        }
    }

    bool EditorRenderServiceImpl::isMeshLoaded(const std::string& meshPath) const
    {
        return offScreenProvider && offScreenProvider->isMeshLoaded(meshPath);
    }

    std::optional<MeshBoundingBox> EditorRenderServiceImpl::getMeshBoundingBox(const std::string& meshPath) const
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

    void EditorRenderServiceImpl::waitForOffScreenIdleDuringPlayModeStop()
    {
        if (!offScreenProvider)
        {
            return;
        }

        try
        {
            offScreenProvider->waitForIdle();
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to wait for offscreen GPU idle during play-mode stop: {}", e.what());
        }
    }

}
