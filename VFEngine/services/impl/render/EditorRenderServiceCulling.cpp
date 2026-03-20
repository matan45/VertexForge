#include "EditorRenderServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/RenderEvents.hpp"

namespace services
{
    void EditorRenderServiceImpl::registerCullingHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::render::SetFrustumCullingCommand>(
            [this](const events::render::SetFrustumCullingCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setFrustumCullingEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetOcclusionCullingCommand>(
            [this](const events::render::SetOcclusionCullingCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setOcclusionCullingEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetLODSelectionCommand>(
            [this](const events::render::SetLODSelectionCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setLODSelectionEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetLODCrossfadeCommand>(
            [this](const events::render::SetLODCrossfadeCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setLODCrossfadeEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetMeshletFrustumCullingCommand>(
            [this](const events::render::SetMeshletFrustumCullingCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setMeshletFrustumCullingEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetMeshletBackfaceCullingCommand>(
            [this](const events::render::SetMeshletBackfaceCullingCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setMeshletBackfaceCullingEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetMeshletOcclusionCullingCommand>(
            [this](const events::render::SetMeshletOcclusionCullingCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setMeshletOcclusionCullingEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetDistanceCullingCommand>(
            [this](const events::render::SetDistanceCullingCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setDistanceCullingEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetDrawDistanceCommand>(
            [this](const events::render::SetDrawDistanceCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setCategoryDistance(cmd.category, cmd.distance);
            });

        dispatcher.registerCommandHandler<events::render::SetShadowDistanceMultiplierCommand>(
            [this](const events::render::SetShadowDistanceMultiplierCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setShadowDistanceMultiplier(cmd.multiplier);
            });

        dispatcher.registerCommandHandler<events::render::SetGlobalLodBiasCommand>(
            [this](const events::render::SetGlobalLodBiasCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setGlobalLodBias(cmd.bias);
            });
    }

    void EditorRenderServiceImpl::registerTerrainRenderHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::render::SetTerrainFrustumCullingCommand>(
            [this](const events::render::SetTerrainFrustumCullingCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setTerrainFrustumCullingEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainMeshletCullingCommand>(
            [this](const events::render::SetTerrainMeshletCullingCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setTerrainMeshletCullingEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetWBOITCommand>(
            [this](const events::render::SetWBOITCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setWBOITEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainRenderingEnabledCommand>(
            [this](const events::render::SetTerrainRenderingEnabledCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setTerrainRenderingEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainLODBiasCommand>(
            [this](const events::render::SetTerrainLODBiasCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setTerrainLODBias(cmd.bias);
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainErrorThresholdCommand>(
            [this](const events::render::SetTerrainErrorThresholdCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setTerrainErrorThreshold(cmd.threshold);
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainTextureScaleCommand>(
            [this](const events::render::SetTerrainTextureScaleCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setTerrainTextureScale(cmd.scale);
            });

        dispatcher.registerCommandHandler<events::render::SetTerrainShadowLODCommand>(
            [this](const events::render::SetTerrainShadowLODCommand& cmd)
            {
                if (offScreenProvider)
                    offScreenProvider->setTerrainShadowLOD(cmd.lod);
            });
    }
}
