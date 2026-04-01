#include "OffScreen.hpp"
#include "OffScreenController.hpp"

namespace controllers {

	void OffScreen::applyPostProcessSettings(const postprocess::PostProcessSettings& settings)
	{
		offScreenController->applyPostProcessSettings(settings);
	}

	postprocess::PostProcessSettings OffScreen::getPostProcessSettings() const
	{
		return offScreenController->getPostProcessSettings();
	}

	void OffScreen::setPostProcessEnabled(bool enabled)
	{
		offScreenController->setPostProcessEnabled(enabled);
	}

	bool OffScreen::isPostProcessEnabled() const
	{
		return offScreenController->isPostProcessEnabled();
	}

	void OffScreen::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider)
	{
		offScreenController->setVFXRuntimeProvider(provider);
	}

	void OffScreen::setTerrainRenderProvider(services::ITerrainRenderProvider* provider)
	{
		offScreenController->setTerrainRenderProvider(provider);
	}

	void OffScreen::setOceanRenderProvider(services::IOceanRenderProvider* provider)
	{
		offScreenController->setOceanRenderProvider(provider);
	}

	void OffScreen::setGrassRenderProvider(services::IGrassRenderProvider* provider)
	{
		offScreenController->setGrassRenderProvider(provider);
	}

	void OffScreen::setVegetationRenderProvider(services::IVegetationRenderProvider* provider)
	{
		offScreenController->setVegetationRenderProvider(provider);
	}

	void OffScreen::setRaycastCursorUV(const glm::vec2& uv)
	{
		offScreenController->setRaycastCursorUV(uv);
	}

	void OffScreen::clearRaycastCursor()
	{
		offScreenController->clearRaycastCursor();
	}

	terrain::TerrainHitResult OffScreen::getTerrainHitResult() const
	{
		return offScreenController->getTerrainHitResult();
	}

	void OffScreen::setBrushOverlayParams(float radius, float falloff, float shape)
	{
		offScreenController->setBrushOverlayParams(radius, falloff, shape);
	}

	bool OffScreen::applyBrushGPU(
		std::vector<float>& heightData,
		const terrain::BrushGPUParams& params)
	{
		return offScreenController->applyBrushGPU(heightData, params);
	}

	render::RenderPassHandler* OffScreen::getRenderPassHandler() const
	{
		return offScreenController ? offScreenController->getRenderPassHandler() : nullptr;
	}

	plugin::RenderHookHandle OffScreen::registerRenderHook(
		plugin::RenderPassHookPoint hookPoint,
		plugin::RenderHookCallback callback)
	{
		if (!offScreenController) return {};
		return offScreenController->registerRenderHook(hookPoint, std::move(callback));
	}

	void OffScreen::unregisterRenderHook(plugin::RenderHookHandle handle)
	{
		if (offScreenController)
			offScreenController->unregisterRenderHook(handle);
	}

	void OffScreen::addTerrainFrustum(const glm::mat4& viewProjection, const glm::vec3& cameraPos)
	{
		if (offScreenController)
			offScreenController->addTerrainFrustum(viewProjection, cameraPos);
	}

	void OffScreen::clearAdditionalTerrainFrustums()
	{
		if (offScreenController)
			offScreenController->clearAdditionalTerrainFrustums();
	}

	void OffScreen::applyAtmosphereSettings(const render::atmosphere::AtmosphereSettings& settings)
	{
		offScreenController->applyAtmosphereSettings(settings);
	}

	render::atmosphere::AtmosphereSettings OffScreen::getAtmosphereSettings() const
	{
		return offScreenController->getAtmosphereSettings();
	}

	void OffScreen::applyCloudSettings(const render::cloud::CloudSettings& settings)
	{
		offScreenController->applyCloudSettings(settings);
	}

	render::cloud::CloudSettings OffScreen::getCloudSettings() const
	{
		return offScreenController->getCloudSettings();
	}

	void OffScreen::setSnowAccumulation(float value)
	{
		offScreenController->setSnowAccumulation(value);
	}

	void OffScreen::setWetness(float value)
	{
		offScreenController->setWetness(value);
	}

	void OffScreen::applyGISettings(const render::gi::GISettings& settings)
	{
		offScreenController->applyGISettings(settings);
	}

	render::gi::GISettings OffScreen::getGISettings() const
	{
		return offScreenController->getGISettings();
	}

	render::gi::GIDebugStats OffScreen::getGIDebugStats() const
	{
		return offScreenController->getGIDebugStats();
	}

	void OffScreen::setGIShowProbes(bool show)
	{
		offScreenController->setGIShowProbes(show);
	}

	void OffScreen::setGIShowCascadeBounds(bool show)
	{
		offScreenController->setGIShowCascadeBounds(show);
	}

	void OffScreen::setGIShowProbeValidity(bool show)
	{
		offScreenController->setGIShowProbeValidity(show);
	}

	void OffScreen::setLightStreamingConfig(const render::lighting::LightStreamingConfig& config)
	{
		offScreenController->setLightStreamingConfig(config);
	}

	render::lighting::LightStreamingConfig OffScreen::getLightStreamingConfig() const
	{
		return offScreenController->getLightStreamingConfig();
	}

	render::lighting::LightStreamingStats OffScreen::getLightStreamingStats() const
	{
		return offScreenController->getLightStreamingStats();
	}

	void OffScreen::registerSectorLights(uint32_t sectorId, const std::vector<uint32_t>& lightEntityIds)
	{
		offScreenController->registerSectorLights(sectorId, lightEntityIds);
	}

	void OffScreen::unregisterSectorLights(uint32_t sectorId)
	{
		offScreenController->unregisterSectorLights(sectorId);
	}

	void OffScreen::setObjectStreamingEnabled(bool enabled)
	{
		offScreenController->setObjectStreamingEnabled(enabled);
	}

	void OffScreen::setObjectStreamingConfig(const render::gpudriven::ObjectStreamConfig& config)
	{
		offScreenController->setObjectStreamingConfig(config);
	}

	render::gpudriven::ObjectStreamConfig OffScreen::getObjectStreamingConfig() const
	{
		return offScreenController->getObjectStreamingConfig();
	}

	render::gpudriven::ObjectStreamingStats OffScreen::getObjectStreamingStats() const
	{
		return offScreenController->getObjectStreamingStats();
	}

	void OffScreen::registerSectorObjects(uint32_t sectorId,
	                                       const std::vector<std::pair<uint64_t, entt::entity>>& entities)
	{
		offScreenController->registerSectorObjects(sectorId, entities);
	}

	void OffScreen::unregisterSectorObjects(uint32_t sectorId)
	{
		offScreenController->unregisterSectorObjects(sectorId);
	}

}
