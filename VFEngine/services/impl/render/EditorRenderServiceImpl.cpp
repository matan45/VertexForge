#include "print/Log.hpp"
#include "EditorRenderServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/render/PostProcessEvents.hpp"
#include "../../events/render/AtmosphereEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentPhysicsLightEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
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

        void* descriptorSet = offScreenProvider->render();

        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = descriptorSet;
        handle.width = viewportWidth;
        handle.height = viewportHeight;

        lastViewportHandle = handle;
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

    void EditorRenderServiceImpl::registerViewportHandlers(events::EventDispatcher& dispatcher)
    {
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

        dispatcher.registerCommandHandler<events::render::SetShowPhysicsDebugCommand>(
            [this](const events::render::SetShowPhysicsDebugCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setShowPhysicsDebug(cmd.show);
                }
            });

        dispatcher.registerQueryHandler<events::render::GetShowPhysicsDebugQuery>(
            [this](const events::render::GetShowPhysicsDebugQuery&)
            {
                return offScreenProvider ? offScreenProvider->getShowPhysicsDebug() : false;
            });

        dispatcher.registerCommandHandler<events::render::SetShowNavmeshDebugCommand>(
            [this](const events::render::SetShowNavmeshDebugCommand& cmd)
            {
                showNavmeshDebug = cmd.show;
                if (offScreenProvider)
                {
                    offScreenProvider->setShowNavmeshDebug(cmd.show);
                }
            });

        dispatcher.registerQueryHandler<events::render::GetShowNavmeshDebugQuery>(
            [this](const events::render::GetShowNavmeshDebugQuery&)
            {
                return showNavmeshDebug;
            });

        dispatcher.registerCommandHandler<events::render::UpdateNavmeshDebugMeshCommand>(
            [this](const events::render::UpdateNavmeshDebugMeshCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->updateNavmeshDebugMesh(cmd.vertices, cmd.indices);
                }
            });

        dispatcher.registerCommandHandler<events::render::ClearNavmeshDebugMeshCommand>(
            [this](const events::render::ClearNavmeshDebugMeshCommand&)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->clearNavmeshDebugMesh();
                }
            });

        dispatcher.registerCommandHandler<events::render::SetViewModeCommand>(
            [this](const events::render::SetViewModeCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setViewMode(cmd.mode);
                }
            });

        dispatcher.registerQueryHandler<events::render::GetViewModeQuery>(
            [this](const events::render::GetViewModeQuery&)
            {
                return offScreenProvider ? offScreenProvider->getViewMode() : 0u;
            });

        dispatcher.registerCommandHandler<events::render::SetShowShadowDebugCommand>(
            [this](const events::render::SetShowShadowDebugCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setShowShadowDebug(cmd.show);
                }
            });

        dispatcher.registerQueryHandler<events::render::GetShowShadowDebugQuery>(
            [this](const events::render::GetShowShadowDebugQuery&)
            {
                return offScreenProvider ? offScreenProvider->getShowShadowDebug() : false;
            });

        dispatcher.registerQueryHandler<events::render::GetCullingStatsQuery>(
            [this](const events::render::GetCullingStatsQuery&)
            {
                return offScreenProvider ? offScreenProvider->getCullingStats() : services::CullingDebugStats{};
            });

        dispatcher.registerCommandHandler<events::render::ApplyShadowSettingsCommand>(
            [this](const events::render::ApplyShadowSettingsCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->applyShadowSettings(cmd.settings);
                }
            });

        dispatcher.registerQueryHandler<events::render::GetShadowStatsQuery>(
            [this](const events::render::GetShadowStatsQuery&)
            {
                return offScreenProvider ? offScreenProvider->getShadowStats() : services::ShadowStats{};
            });

        dispatcher.registerCommandHandler<events::render::SetUIViewportOffsetCommand>(
            [this](const events::render::SetUIViewportOffsetCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setUIViewportOffset(cmd.offset, cmd.panelSize);
                }
            });
    }

    void EditorRenderServiceImpl::registerCullingHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::render::SetFrustumCullingCommand>(
            [this](const events::render::SetFrustumCullingCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setFrustumCullingEnabled(cmd.enabled);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetOcclusionCullingCommand>(
            [this](const events::render::SetOcclusionCullingCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setOcclusionCullingEnabled(cmd.enabled);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetLODSelectionCommand>(
            [this](const events::render::SetLODSelectionCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setLODSelectionEnabled(cmd.enabled);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetMeshletFrustumCullingCommand>(
            [this](const events::render::SetMeshletFrustumCullingCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setMeshletFrustumCullingEnabled(cmd.enabled);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetMeshletBackfaceCullingCommand>(
            [this](const events::render::SetMeshletBackfaceCullingCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setMeshletBackfaceCullingEnabled(cmd.enabled);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetDistanceCullingCommand>(
            [this](const events::render::SetDistanceCullingCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setDistanceCullingEnabled(cmd.enabled);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetDrawDistanceCommand>(
            [this](const events::render::SetDrawDistanceCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setCategoryDistance(cmd.category, cmd.distance);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetShadowDistanceMultiplierCommand>(
            [this](const events::render::SetShadowDistanceMultiplierCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setShadowDistanceMultiplier(cmd.multiplier);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetGlobalLodBiasCommand>(
            [this](const events::render::SetGlobalLodBiasCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setGlobalLodBias(cmd.bias);
                }
            });
    }

    void EditorRenderServiceImpl::registerTerrainRenderHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::render::SetTerrainFrustumCullingCommand>(
            [this](const events::render::SetTerrainFrustumCullingCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setTerrainFrustumCullingEnabled(cmd.enabled);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainMeshletCullingCommand>(
            [this](const events::render::SetTerrainMeshletCullingCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setTerrainMeshletCullingEnabled(cmd.enabled);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetWBOITCommand>(
            [this](const events::render::SetWBOITCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setWBOITEnabled(cmd.enabled);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainRenderingEnabledCommand>(
            [this](const events::render::SetTerrainRenderingEnabledCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setTerrainRenderingEnabled(cmd.enabled);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainLODBiasCommand>(
            [this](const events::render::SetTerrainLODBiasCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setTerrainLODBias(cmd.bias);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainErrorThresholdCommand>(
            [this](const events::render::SetTerrainErrorThresholdCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setTerrainErrorThreshold(cmd.threshold);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainTextureScaleCommand>(
            [this](const events::render::SetTerrainTextureScaleCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setTerrainTextureScale(cmd.scale);
                }
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainShadowLODCommand>(
            [this](const events::render::SetTerrainShadowLODCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->setTerrainShadowLOD(cmd.lod);
                }
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
    }

    void EditorRenderServiceImpl::registerAtmosphereHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::atmosphere::ApplyAtmosphereSettingsCommand>(
            [this](const events::atmosphere::ApplyAtmosphereSettingsCommand& cmd)
            {
                if (offScreenProvider)
                {
                    offScreenProvider->applyAtmosphereSettings(cmd.settings);
                }

                // Reset stale handle if entity was deleted (e.g. scene clear)
                if (autoCreatedSunEntity.isValid())
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto entity = static_cast<entt::entity>(static_cast<uint32_t>(autoCreatedSunEntity.id));
                    if (!registry.valid(entity))
                    {
                        autoCreatedSunEntity = {};
                    }
                }

                // Auto-create a directional light ("Sun") if enabling and none exists
                if (cmd.settings.enabled)
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto dirLightView = registry.view<components::DirectionalLightComponent>();
                    if (dirLightView.begin() == dirLightView.end())
                    {
                        auto& disp = events::EventDispatcher::instance();

                        events::scene::CreateEntityCommand createCmd;
                        createCmd.name = "Sun";
                        auto handle = disp.execute(createCmd);

                        events::scene::AddDirectionalLightComponentCommand addLight;
                        addLight.entity = handle;
                        disp.execute(addLight);

                        // Rotate to match fallback elevation (45 deg down from zenith)
                        events::scene::SetTransformCommand xformCmd;
                        xformCmd.entity = handle;
                        xformCmd.transform.position = {0.0f, 0.0f, 0.0f};
                        xformCmd.transform.rotation = {-cmd.settings.sunElevation, cmd.settings.sunAzimuth, 0.0f};
                        xformCmd.transform.scale = {1.0f, 1.0f, 1.0f};
                        disp.execute(xformCmd);

                        autoCreatedSunEntity = handle;
                    }
                }
                // Remove auto-created Sun when disabling
                else if (autoCreatedSunEntity.isValid())
                {
                    auto& disp = events::EventDispatcher::instance();
                    events::scene::DeleteEntityCommand deleteCmd;
                    deleteCmd.entity = autoCreatedSunEntity;
                    disp.execute(deleteCmd);
                    autoCreatedSunEntity = {};
                }
            });

        dispatcher.registerQueryHandler<events::atmosphere::GetAtmosphereSettingsQuery>(
            [this](const events::atmosphere::GetAtmosphereSettingsQuery&)
            {
                return offScreenProvider
                           ? offScreenProvider->getAtmosphereSettings()
                           : render::atmosphere::AtmosphereSettings{};
            });

        dispatcher.registerCommandHandler<events::atmosphere::SetAtmosphereEnabledCommand>(
            [this](const events::atmosphere::SetAtmosphereEnabledCommand& cmd)
            {
                if (offScreenProvider)
                {
                    auto settings = offScreenProvider->getAtmosphereSettings();
                    settings.enabled = cmd.enabled;
                    offScreenProvider->applyAtmosphereSettings(settings);
                }
            });

        dispatcher.registerQueryHandler<events::atmosphere::GetAtmosphereEnabledQuery>(
            [this](const events::atmosphere::GetAtmosphereEnabledQuery&)
            {
                return offScreenProvider ? offScreenProvider->getAtmosphereSettings().enabled : false;
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

}
