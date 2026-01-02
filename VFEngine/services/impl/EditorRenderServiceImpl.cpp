#include "EditorRenderServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/EditorModeEvents.hpp"
#include "print/EditorLogger.hpp"
#include <filesystem>

namespace services
{
    EditorRenderServiceImpl::EditorRenderServiceImpl(IOffScreenProvider* offScreenProvider,
                                                     IEditorTextureProvider* textureProvider)
        : offScreenProvider(offScreenProvider)
          , textureProvider(textureProvider)
    {
    }

    EditorRenderServiceImpl::~EditorRenderServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (meshDataChangedToken.isValid())
        {
            dispatcher.unsubscribe(meshDataChangedToken);
        }

        if (editorModeChangedToken.isValid())
        {
            dispatcher.unsubscribe(editorModeChangedToken);
        }
    }

    ViewportTextureHandle EditorRenderServiceImpl::getViewportTexture()
    {
        if (!offScreenProvider)
        {
            return ViewportTextureHandle{};
        }

        frameCounter++;
        prepareGrid();
        prepareCameras();
        prepareFrameMeshes();
        prepareFrameBillboards();
        prepareFrameCameraFrustums();
        prepareFrameAudioSpheres();

        void* descriptorSet = offScreenProvider->render();

        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = descriptorSet;
        handle.width = viewportWidth;
        handle.height = viewportHeight;

        return handle;
    }

    void EditorRenderServiceImpl::resizeViewport(uint32_t width, uint32_t height)
    {
        if (width == viewportWidth && height == viewportHeight)
        {
            return;
        }

        viewportWidth = width;
        viewportHeight = height;
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

        dispatcher.registerCommandHandler<events::render::SetShowBillboardIconsCommand>(
            [this](const events::render::SetShowBillboardIconsCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setShowBillboardIcons(cmd.show);
                }
            });

        dispatcher.registerCommandHandler<events::render::LoadBillboardAtlasCommand>(
            [this](const events::render::LoadBillboardAtlasCommand& cmd)
            {
                return offScreenProvider ? offScreenProvider->loadBillboardAtlas(cmd.atlasPath) : false;
            });

        dispatcher.registerQueryHandler<events::render::GetShowBillboardIconsQuery>(
            [this](const events::render::GetShowBillboardIconsQuery&)
            {
                return offScreenProvider ? offScreenProvider->getShowBillboardIcons() : true;
            });

        dispatcher.registerCommandHandler<events::render::SetShowDebugRenderingCommand>(
            [this](const events::render::SetShowDebugRenderingCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setShowDebugRendering(cmd.show);
                }
            });

        dispatcher.registerQueryHandler<events::render::GetShowDebugRenderingQuery>(
            [this](const events::render::GetShowDebugRenderingQuery&)
            {
                return offScreenProvider ? offScreenProvider->getShowDebugRendering() : true;
            });

        dispatcher.registerCommandHandler<events::render::SetShowGridCommand>(
            [this](const events::render::SetShowGridCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setShowGrid(cmd.show);
                }
            });

        dispatcher.registerQueryHandler<events::render::GetShowGridQuery>(
            [this](const events::render::GetShowGridQuery&)
            {
                return offScreenProvider ? offScreenProvider->getShowGrid() : true;
            });

        dispatcher.registerQueryHandler<events::render::GetCullingStatsQuery>(
            [this](const events::render::GetCullingStatsQuery&)
            {
                return offScreenProvider ? offScreenProvider->getCullingStats() : services::CullingDebugStats{};
            });

        meshDataChangedToken = dispatcher.subscribe<events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification)
            {
                if (!notification.meshPath.empty() && !isMeshLoaded(notification.meshPath))
                {
                    loadMesh(notification.meshPath);
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
    }

    std::string EditorRenderServiceImpl::loadMesh(const std::string& meshPath)
    {
        if (!offScreenProvider)
        {
            return "";
        }
        return offScreenProvider->meshLoad(meshPath);
    }

    void EditorRenderServiceImpl::unloadMesh(const std::string& meshId)
    {
        if (offScreenProvider)
        {
            offScreenProvider->meshUnload(meshId);
        }
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

    std::vector<std::string> EditorRenderServiceImpl::getLoadedMeshes() const
    {
        if (!offScreenProvider)
        {
            return {};
        }
        return offScreenProvider->getLoadedMeshes();
    }

    void EditorRenderServiceImpl::prepareCameras()
    {
        if (offScreenProvider)
        {
            offScreenProvider->prepareCameras();
        }
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

    void EditorRenderServiceImpl::prepareFrameMeshes()
    {
        if (offScreenProvider)
        {
            offScreenProvider->prepareFrameMeshes();
        }
    }

    void EditorRenderServiceImpl::prepareFrameBillboards()
    {
        if (!offScreenProvider)
        {
            return;
        }

        offScreenProvider->prepareFrameBillboards();
    }

    void EditorRenderServiceImpl::prepareFrameCameraFrustums()
    {
        if (!offScreenProvider)
        {
            return;
        }

        offScreenProvider->prepareFrameCameraFrustums();
    }

    void EditorRenderServiceImpl::prepareFrameAudioSpheres()
    {
        if (!offScreenProvider)
        {
            return;
        }

        offScreenProvider->prepareFrameAudioSpheres();
    }

    void EditorRenderServiceImpl::prepareGrid()
    {
        if (!offScreenProvider)
        {
            return;
        }

        offScreenProvider->prepareGrid();
    }
}
