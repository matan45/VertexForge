#include "OffScreenController.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../render/mesh/MeshStreamManager.hpp"
#include "../render/gi/RadianceCascadeManager.hpp"
#include "../render/gi/SSGIPipeline.hpp"
#include "../render/gi/GIDebugRenderer.hpp"
#include "scene/EntityRegistry.hpp"

namespace controllers
{
    void OffScreenController::applyAtmosphereSettings(const render::atmosphere::AtmosphereSettings& settings)
    {
        currentAtmosphereSettings = settings;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        renderHandler->applyAtmosphereSettings(settings);
    }

    render::atmosphere::AtmosphereSettings OffScreenController::getAtmosphereSettings() const
    {
        return currentAtmosphereSettings;
    }

    void OffScreenController::applyCloudSettings(const render::cloud::CloudSettings& settings)
    {
        currentCloudSettings = settings;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        renderHandler->applyCloudSettings(settings);
    }

    render::cloud::CloudSettings OffScreenController::getCloudSettings() const
    {
        return currentCloudSettings;
    }

    void OffScreenController::setSnowAccumulation(float value)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        renderHandler->setSnowAccumulation(value);
    }

    void OffScreenController::setWetness(float value)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        renderHandler->setWetness(value);
    }

    void OffScreenController::applyGISettings(const render::gi::GISettings& settings)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu)
            gpu->applyGISettings(settings);

        if (settings.enabled && settings.ssgiEnabled)
        {
            renderHandler->initSSGI();
            if (auto* ssgi = renderHandler->getSSGIPipeline())
                ssgi->updateSettings(settings);
        }
        else
        {
            renderHandler->resetSSGI();
        }
    }

    render::gi::GISettings OffScreenController::getGISettings() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu)
            return gpu->getGISettings();
        return {};
    }

    render::gi::GIDebugStats OffScreenController::getGIDebugStats() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getGICascadeManager())
            return gpu->getGICascadeManager()->getDebugStats();
        return {};
    }

    void OffScreenController::setGIShowProbes(bool show)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getGIDebugRenderer())
            gpu->getGIDebugRenderer()->setShowProbes(show);
    }

    void OffScreenController::setGIShowCascadeBounds(bool show)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getGIDebugRenderer())
            gpu->getGIDebugRenderer()->setShowCascadeBounds(show);
    }

    void OffScreenController::setGIShowProbeValidity(bool show)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getGIDebugRenderer())
            gpu->getGIDebugRenderer()->setShowProbeValidity(show);
    }

    void OffScreenController::setLightStreamingConfig(const render::lighting::LightStreamingConfig& config)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getLightStreamManager())
            gpu->getLightStreamManager()->setConfig(config);
    }

    render::lighting::LightStreamingConfig OffScreenController::getLightStreamingConfig() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getLightStreamManager())
            return gpu->getLightStreamManager()->getConfig();
        return {};
    }

    render::lighting::LightStreamingStats OffScreenController::getLightStreamingStats() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getLightStreamManager())
            return gpu->getLightStreamManager()->getStats();
        return {};
    }

    void OffScreenController::registerSectorLights(uint32_t sectorId, const std::vector<uint32_t>& lightEntityIds)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getLightStreamManager())
        {
            gpu->getLightStreamManager()->registerSectorLights(sectorId, lightEntityIds);

            // Pre-warm shadow maps for shadow-casting lights in the loaded sector
            if (gpu->getLightBufferManager())
                gpu->getLightBufferManager()->preWarmShadowsForLights(lightEntityIds);
        }
    }

    void OffScreenController::unregisterSectorLights(uint32_t sectorId)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getLightStreamManager())
            gpu->getLightStreamManager()->unregisterSectorLights(sectorId);
    }

    void OffScreenController::setObjectStreamingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu)
            gpu->setObjectStreamingEnabled(enabled);
    }

    void OffScreenController::setObjectStreamingConfig(const render::gpudriven::ObjectStreamConfig& config)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getObjectStreamManager())
            gpu->getObjectStreamManager()->setConfig(config);
    }

    render::gpudriven::ObjectStreamConfig OffScreenController::getObjectStreamingConfig() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getObjectStreamManager())
            return gpu->getObjectStreamManager()->getConfig();
        return {};
    }

    render::gpudriven::ObjectStreamingStats OffScreenController::getObjectStreamingStats() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getObjectStreamManager())
            return gpu->getObjectStreamManager()->getStats();
        return {};
    }

    void OffScreenController::registerSectorObjects(
        uint32_t sectorId,
        const std::vector<std::pair<uint64_t, entt::entity>>& entities)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getObjectStreamManager())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            gpu->getObjectStreamManager()->registerSectorObjects(sectorId, entities, registry);
        }
    }

    void OffScreenController::unregisterSectorObjects(uint32_t sectorId)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getObjectStreamManager())
            gpu->getObjectStreamManager()->unregisterSectorObjects(sectorId);
    }

    bool OffScreenController::registerHLODMesh(const render::mesh::InMemoryMeshData& meshData)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return false;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (!gpu) return false;

        auto* streamManager = gpu->getMeshStreamManager();
        if (!streamManager) return false;

        return streamManager->registerInMemoryMesh(meshData);
    }

    void OffScreenController::releaseHLODMesh(const std::string& meshKey)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (!gpu) return;

        if (auto* streamManager = gpu->getMeshStreamManager())
            streamManager->releaseInMemoryMesh(meshKey);
    }
}
