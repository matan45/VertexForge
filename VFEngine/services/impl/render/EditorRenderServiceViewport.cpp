#include "EditorRenderServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/render/PostProcessEvents.hpp"
#include "../../events/render/AtmosphereEvents.hpp"
#include "../../events/render/CloudEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentPhysicsLightEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace services
{
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
                    offScreenProvider->setShowBillboardIcons(cmd.show);
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
                    offScreenProvider->setShowDebugRendering(cmd.show);
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
                    offScreenProvider->setShowGrid(cmd.show);
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
                    offScreenProvider->setShowPhysicsDebug(cmd.show);
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
                    offScreenProvider->setShowNavmeshDebug(cmd.show);
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
                    offScreenProvider->updateNavmeshDebugMesh(cmd.vertices, cmd.indices);
            });

        dispatcher.registerCommandHandler<events::render::ClearNavmeshDebugMeshCommand>(
            [this](const events::render::ClearNavmeshDebugMeshCommand&)
            {
                if (offScreenProvider)
                    offScreenProvider->clearNavmeshDebugMesh();
            });

        dispatcher.registerCommandHandler<events::render::SetViewModeCommand>(
            [this](const events::render::SetViewModeCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setViewMode(cmd.mode);
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
                    offScreenProvider->setShowShadowDebug(cmd.show);
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

        dispatcher.registerQueryHandler<events::render::GetGPUPipelineStatusQuery>(
            [this](const events::render::GetGPUPipelineStatusQuery&)
            {
                return offScreenProvider ? offScreenProvider->getGPUPipelineStatus() : services::GPUPipelineStatus{};
            });

        dispatcher.registerCommandHandler<events::render::ApplyShadowSettingsCommand>(
            [this](const events::render::ApplyShadowSettingsCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->applyShadowSettings(cmd.settings);
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
                    offScreenProvider->setUIViewportOffset(cmd.offset, cmd.panelSize);
            });
    }

    void EditorRenderServiceImpl::registerAtmosphereHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::atmosphere::ApplyAtmosphereSettingsCommand>(
            [this](const events::atmosphere::ApplyAtmosphereSettingsCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->applyAtmosphereSettings(cmd.settings);

                if (autoCreatedSunEntity.isValid())
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto entity = static_cast<entt::entity>(static_cast<uint32_t>(autoCreatedSunEntity.id));
                    if (!registry.valid(entity))
                        autoCreatedSunEntity = {};
                }

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

                        events::scene::SetTransformCommand xformCmd;
                        xformCmd.entity = handle;
                        xformCmd.transform.position = {0.0f, 0.0f, 0.0f};
                        xformCmd.transform.rotation = {-cmd.settings.sunElevation, cmd.settings.sunAzimuth, 0.0f};
                        xformCmd.transform.scale = {1.0f, 1.0f, 1.0f};
                        disp.execute(xformCmd);

                        autoCreatedSunEntity = handle;
                    }
                }
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

    void EditorRenderServiceImpl::registerCloudHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::cloud::ApplyCloudSettingsCommand>(
            [this](const events::cloud::ApplyCloudSettingsCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->applyCloudSettings(cmd.settings);
            });

        dispatcher.registerQueryHandler<events::cloud::GetCloudSettingsQuery>(
            [this](const events::cloud::GetCloudSettingsQuery&)
            {
                return offScreenProvider ? offScreenProvider->getCloudSettings()
                                         : render::cloud::CloudSettings{};
            });

        dispatcher.registerCommandHandler<events::cloud::SetCloudEnabledCommand>(
            [this](const events::cloud::SetCloudEnabledCommand& cmd)
            {
                if (offScreenProvider)
                {
                    auto settings = offScreenProvider->getCloudSettings();
                    settings.enabled = cmd.enabled;
                    offScreenProvider->applyCloudSettings(settings);
                }
            });

        dispatcher.registerQueryHandler<events::cloud::GetCloudEnabledQuery>(
            [this](const events::cloud::GetCloudEnabledQuery&)
            {
                return offScreenProvider ? offScreenProvider->getCloudSettings().enabled : false;
            });
    }
}
