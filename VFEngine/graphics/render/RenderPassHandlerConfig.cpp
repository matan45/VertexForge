#include "RenderPassHandler.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "vegetation/VegetationTypes.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/VegetationComponents.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "billboard/BillboardPipeline.hpp"
#include "text/TextPipeline.hpp"
#include "ui/UIRenderPipeline.hpp"
#include "ui/UITextPipeline.hpp"
#include "DebugRenderer.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "gpudriven/terrain/TerrainRaycastPipeline.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "volumetric/VolumetricFogComposite.hpp"
#include "gi/SSGIPipeline.hpp"
#include "ssr/SSRPipeline.hpp"
#include "transparency/WBOITPipeline.hpp"
#include "decal/DecalPipeline.hpp"
#include "atmosphere/AtmospherePipeline.hpp"
#include "atmosphere/SkyEnvironmentCapture.hpp"
#include "ibl/HdrEnvironmentCapture.hpp"
#include "probe/ReflectionProbeManager.hpp"
#include "components/Components.hpp"
#include "cloud/CloudPipeline.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "IBL.hpp"
#include "material/MaterialTextureCache.hpp"
#include "../../services/providers/terrain/ITerrainRenderProvider.hpp"
#include "gpudriven/terrain/TerrainStreamManager.hpp"
#include "../../services/providers/terrain/IOceanRenderProvider.hpp"
#include "../../services/providers/vegetation/IGrassRenderProvider.hpp"
#include "../../services/providers/vegetation/IVegetationRenderProvider.hpp"
#include "../../services/providers/vfx/IVFXRuntimeProvider.hpp"
#include "terrain/TerrainTile.hpp"
#include "material/MaterialTypes.hpp"

namespace render
{
    void RenderPassHandler::setDeletionQueue(core::DeferredDeletionQueue* queue)
    {
        deletionQueue = queue; // VK-1574: reused when retiring old HDR source textures
        if (hdrEnvCapture) hdrEnvCapture->setDeletionQueue(queue);
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized) gpuDrivenRenderer->setDeletionQueue(queue);
        if (textPipeline) textPipeline->setDeletionQueue(queue);
        if (uiPipeline) uiPipeline->setDeletionQueue(queue);
        if (uiTextPipeline) uiTextPipeline->setDeletionQueue(queue);
        if (billboardPipeline) billboardPipeline->setDeletionQueue(queue);
        if (postProcessPipeline) postProcessPipeline->setDeletionQueue(queue);
        if (debugRenderer) debugRenderer->setDeletionQueue(queue);
    }

    void RenderPassHandler::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider) { vfxRuntimeProvider = provider; }
    void RenderPassHandler::setVFXDistanceCullingEnabled(bool enabled) { if (vfxRuntimeProvider) vfxRuntimeProvider->setDistanceCullingEnabled(enabled); }
    void RenderPassHandler::setVFXDrawDistance(float distance) { if (vfxRuntimeProvider) vfxRuntimeProvider->setMaxDrawDistance(distance); }
    void RenderPassHandler::setBillboardDistanceCullingEnabled(bool enabled) { if (billboardPipelineInitialized && billboardPipeline) billboardPipeline->setDistanceCullingEnabled(enabled); }
    void RenderPassHandler::setBillboardDrawDistance(float distance) { if (billboardPipelineInitialized && billboardPipeline) billboardPipeline->setMaxDrawDistance(distance); }
    void RenderPassHandler::setTerrainDistanceCullingEnabled(bool enabled) { if (terrainRenderProvider) terrainRenderProvider->setDistanceCullingEnabled(enabled); }
    void RenderPassHandler::setTerrainDrawDistance(float distance) { if (terrainRenderProvider) terrainRenderProvider->setMaxDrawDistance(distance); }

    void RenderPassHandler::setTerrainRenderProvider(services::ITerrainRenderProvider* provider)
    {
        terrainRenderProvider = provider;
        if (provider && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTileDataLoader(
                [provider](terrain::TerrainTile& tile, uint8_t lod) -> bool { return provider->ensureTileLODData(tile, lod); });
            gpuDrivenRenderer->setTileRAMEvictor(
                [provider](terrain::TerrainTile& tile) { provider->releaseTileRAMData(tile); });
            gpuDrivenRenderer->setTileLoadContextProvider(
                [provider](const render::gpudriven::TerrainTileKey& key) -> render::gpudriven::TileLoadContext {
                    auto serviceResult = provider->prepareTileLoadContext(key.coordX, key.coordZ);
                    render::gpudriven::TileLoadContext ctx;
                    ctx.key = key;
                    ctx.filePath = std::move(serviceResult.filePath);
                    ctx.indexEntry = serviceResult.indexEntry;
                    ctx.hasMeshletCache = serviceResult.hasMeshletCache;
                    ctx.valid = serviceResult.valid;
                    return ctx;
                });
        }
    }

    void RenderPassHandler::setGrassRenderProvider(services::IGrassRenderProvider* provider)
    {
        grassRenderProvider = provider;
        if (provider && gpuDrivenRenderer)
        {
            auto* renderer = gpuDrivenRenderer.get();
            provider->setAddTileCallback([renderer](int32_t x, int32_t z) { renderer->addVegetationTile(x, z); });
            provider->setRemoveTileCallback([renderer](int32_t x, int32_t z) { renderer->removeVegetationTile(x, z); });
            provider->setMarkDirtyCallback([renderer](int32_t x, int32_t z) { renderer->markVegetationTileDirty(x, z); });
            provider->setOnBillboardPaletteChanged([renderer](const std::vector<::vegetation::BillboardPaletteEntry>& entries, int32_t activeEntry)
            {
                renderer->setBillboardPaletteFromEntries(entries);
                renderer->setActiveBillboardEntry(activeEntry);
            });

            renderer->setBillboardPaletteLoader([]() -> std::vector<::vegetation::BillboardPaletteEntry> {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::GrassComponent>();
                for (auto entity : view)
                    return view.get<components::GrassComponent>(entity).billboardPalette;
                return {};
            });
        }
    }

    void RenderPassHandler::setVegetationRenderProvider(services::IVegetationRenderProvider* provider)
    {
        if (provider && gpuDrivenRenderer)
        {
            auto* renderer = gpuDrivenRenderer.get();
            provider->setAddTileCallback([renderer](int32_t x, int32_t z) { renderer->addVegetationTile(x, z); });
            provider->setRemoveTileCallback([renderer](int32_t x, int32_t z) { renderer->removeVegetationTile(x, z); });
            provider->setMarkDirtyCallback([renderer](int32_t x, int32_t z) { renderer->markVegetationTileDirty(x, z); });
        }
    }

    void RenderPassHandler::setOceanRenderProvider(services::IOceanRenderProvider* provider) { oceanRenderProvider = provider; }

    void RenderPassHandler::clearTerrainData() { if (gpuDrivenRenderer) gpuDrivenRenderer->clearTerrainData(); }
    void RenderPassHandler::evictTerrainTile(int32_t coordX, int32_t coordZ) { if (gpuDrivenRenderer) gpuDrivenRenderer->evictTerrainTile(coordX, coordZ); }
    void RenderPassHandler::setSelectedTerrainTile(int32_t coordX, int32_t coordZ) { if (gpuDrivenRenderer) gpuDrivenRenderer->setSelectedTerrainTile(coordX, coordZ); }
    void RenderPassHandler::clearSelectedTerrainTile() { if (gpuDrivenRenderer) gpuDrivenRenderer->clearSelectedTerrainTile(); }
    void RenderPassHandler::addTerrainFrustum(const math::Frustum& frustum, const glm::vec3& cameraPos) { additionalTerrainFrustums.emplace_back(frustum, cameraPos); }
    void RenderPassHandler::clearAdditionalTerrainFrustums() { additionalTerrainFrustums.clear(); }
    void RenderPassHandler::clearWaterData() { if (gpuDrivenRenderer) gpuDrivenRenderer->clearWaterData(); }

    void RenderPassHandler::reinitMeshPipelineWithDefaults()
    {
        if (!meshPipelineInitialized) return;
        hdrCaptureMeshBound = false; // VK-1574: mesh no longer bound to the HDR capture's live maps
        device.getLogicalDevice().waitIdle();
        meshPipeline->cleanUpForReinit();
        meshPipeline->initWithDefaults();

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            gpuDrivenRenderer->updateFormats({swapChain.getSceneColorFormat()}, swapChain.getSwapchainDepthStencilFormat(), meshPipeline->getIBLDescriptorSetLayout());
        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
            vfxRuntimeProvider->recreate(swapChain.getSceneColorFormat(), swapChain.getSwapchainDepthStencilFormat());
    }

    void RenderPassHandler::reinitMeshPipelineWithIBL()
    {
        if (!meshPipelineInitialized || !iblRenderer->isInitialized()) return;
        hdrCaptureMeshBound = false; // VK-1574

        device.getLogicalDevice().waitIdle();
        meshPipeline->cleanUpForReinit();

        const auto& irradiance = iblRenderer->getIrradianceImage();
        const auto& prefilter = iblRenderer->getPrefilterImage();
        const auto& brdfLUT = iblRenderer->getBrdfLUTImage();
        meshPipeline->init(irradiance, prefilter, brdfLUT);

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            gpuDrivenRenderer->updateFormats({swapChain.getSceneColorFormat()}, swapChain.getSwapchainDepthStencilFormat(), meshPipeline->getIBLDescriptorSetLayout());
        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
            vfxRuntimeProvider->recreate(swapChain.getSceneColorFormat(), swapChain.getSwapchainDepthStencilFormat());
    }

    // VK-1569: point the mesh IBL descriptor at the dynamic-ambient live maps (irradiance + prefilter
    // captured from the atmosphere) with the capture's own static BRDF LUT. One-time reinit on the
    // OFF->ON toggle; per-frame capture then renders into these same images (stable views).
    void RenderPassHandler::reinitMeshPipelineWithDynamicAmbient()
    {
        if (!meshPipelineInitialized || !skyEnvCapture || !skyEnvCapture->isInitialized()) return;
        hdrCaptureMeshBound = false; // VK-1574: sky now owns the mesh ambient (mutually exclusive)

        device.getLogicalDevice().waitIdle();
        meshPipeline->cleanUpForReinit();
        meshPipeline->init(skyEnvCapture->getIrradianceLive(), skyEnvCapture->getPrefilterLive(),
                           skyEnvCapture->getBrdfLUT());

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            gpuDrivenRenderer->updateFormats({swapChain.getSceneColorFormat()}, swapChain.getSwapchainDepthStencilFormat(), meshPipeline->getIBLDescriptorSetLayout());
        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
            vfxRuntimeProvider->recreate(swapChain.getSceneColorFormat(), swapChain.getSwapchainDepthStencilFormat());
    }

    // VK-1574: point the mesh IBL descriptor at the HDR capture's live maps (irradiance + prefilter
    // baked from the equirect HDR) with the capture's own static BRDF LUT. One-time reinit on the
    // first bind; the per-frame HdrEnvCapture pass then renders into these same images (stable views).
    // VK-1577 — reflection probe frame hook. Runs at the render-texture point, before the frame
    // graph, because capturing a probe face means rendering the scene from that face's camera and
    // that is a separate submit.
    //
    // The manager is created LAZILY on first sighting of a ReflectionProbeComponent: a project with
    // no probes never allocates the ~8 MiB of cubes, never creates the capture viewport, and never
    // compiles the probe shader permutation.
    void RenderPassHandler::tickReflectionProbes()
    {
        if (!meshPipelineInitialized) return;

        if (!reflectionProbes)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.view<components::ReflectionProbeComponent>().empty())
                return; // no probes in this scene — stay completely inert

            reflectionProbes = std::make_unique<probe::ReflectionProbeManager>(device, swapChain);
            reflectionProbes->init(device.getStagingCommandPool());

            // Bind the (still empty) probe cubes + SSBO into set 0 immediately. Every slot is a
            // valid image from the moment it exists, so the descriptor never points at nothing.
            meshPipeline->setReflectionProbeResources(
                reflectionProbes->getBufferManager().getBuffer(),
                reflectionProbes->getProbeCubes());
        }

        reflectionProbes->tickSceneCapture(this);
        syncReflectionProbeResources();
    }

    void RenderPassHandler::syncReflectionProbeResources()
    {
        if (!reflectionProbes) return;

        // Edge-triggered: the permutation flips at most once per scene, on the first probe to
        // finish baking (and back again if every probe is removed).
        const bool wantPermutation = reflectionProbes->hasAnyBakedProbe();
        if (wantPermutation != reflectionProbePermutationActive)
        {
            reflectionProbePermutationActive = wantPermutation;
            if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            {
                gpuDrivenRenderer->setReflectionProbesEnabled(wantPermutation);
            }
        }
    }

    void RenderPassHandler::reinitMeshPipelineWithHdrCapture()
    {
        if (!meshPipelineInitialized || !hdrEnvCapture || !hdrEnvCapture->isInitialized()) return;

        device.getLogicalDevice().waitIdle();
        meshPipeline->cleanUpForReinit();
        meshPipeline->init(hdrEnvCapture->getIrradianceLive(), hdrEnvCapture->getPrefilterLive(),
                           hdrEnvCapture->getBrdfLUT());

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            gpuDrivenRenderer->updateFormats({swapChain.getSceneColorFormat()}, swapChain.getSwapchainDepthStencilFormat(), meshPipeline->getIBLDescriptorSetLayout());
        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
            vfxRuntimeProvider->recreate(swapChain.getSceneColorFormat(), swapChain.getSwapchainDepthStencilFormat());

        hdrCaptureMeshBound = true;
    }

    // VK-1574: apply an HDR environment without hitching. The .vfHdr decode is off-thread; the old
    // environment (skybox + ambient) stays bound until the new bake publishes. On the FIRST bind we
    // do one blocking full capture (scene load already blocks) so the first frame samples valid
    // ambient; subsequent applies ride the per-frame time-sliced HdrEnvCapture pass.
    void RenderPassHandler::applyHdrEnvironment(std::string_view path)
    {
        if (!hdrEnvCapture)
        {
            hdrEnvCapture = std::make_unique<ibl::HdrEnvironmentCapture>(device);
            hdrEnvCapture->setDeletionQueue(deletionQueue);
        }
        if (!hdrEnvCapture->isInitialized())
            hdrEnvCapture->init(device.getStagingCommandPool());

        hdrEnvCapture->setSource(std::string(path)); // non-blocking: kicks the off-thread decode

        const bool dynamicAmbient = atmospherePipeline && atmospherePipeline->isInitialized() &&
                                    atmospherePipeline->getSettings().enabled &&
                                    atmospherePipeline->getSettings().dynamicAmbient;

        if (dynamicAmbient)
        {
            // The dynamic sky owns the mesh ambient AND draws the sky while enabled, so the HDR maps
            // are not consumed yet. Keep the (already-set) source ready; applyAtmosphereSettings bakes
            // it and rebinds the mesh + skybox on the dynamicAmbient ON->OFF edge. Baking now would be
            // wasted work.
            return;
        }

        if (!hdrCaptureMeshBound)
        {
            // First bind: one-time blocking bake so the first frame samples valid ambient (no gray
            // flash), then bind the mesh + skybox to the live maps once.
            hdrEnvCapture->captureBlocking(hdrEnvCapture->currentEpoch());
            reinitMeshPipelineWithHdrCapture();
            iblRenderer->initSkybox(hdrEnvCapture->getEnvLive());
        }
        // else: subsequent apply — old live maps stay bound; the per-frame HdrEnvCapture pass
        // time-slices the new bake and publish() swaps env + ambient atomically. No reinit, no hitch.
    }

    // VK-1574: drop the HDR environment — tear down the skybox and revert mesh ambient BEFORE
    // destroying the capture's live maps, so no consumer samples a freed image.
    void RenderPassHandler::removeHdrEnvironment()
    {
        iblRenderer->remove();                    // stops + destroys the skybox (was bound to envLive)
        if (meshPipelineInitialized)
            reinitMeshPipelineWithDefaults();     // rebinds the mesh IBL descriptor off the hdr live maps
        if (hdrEnvCapture)
            hdrEnvCapture->cleanup();             // now safe to destroy env/irradiance/prefilter live maps
    }

    // VK-1569: quantized hash of the sky state (sun direction + time-of-day + weather ambient). The
    // capture starts a fresh cycle only when this changes after the previous cycle published, so a
    // static sky is captured once while a changing sky rolls with ~1-cycle latency.
    uint64_t RenderPassHandler::computeAmbientCaptureEpoch() const
    {
        glm::vec3 sunDir{0.0f, 1.0f, 0.0f};
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            if (auto* lbm = gpuDrivenRenderer->getLightBufferManager())
            {
                if (auto d = lbm->getFirstDirectionalLightDirection())
                    sunDir = *d;
            }
        }
        float timeOfDay = 12.0f;
        float ambient = 1.0f;
        if (atmospherePipeline)
        {
            const auto s = atmospherePipeline->getSettings();
            timeOfDay = s.timeOfDay;
            ambient = s.ambientIntensity;
        }
        auto quant = [](float v, float step) { return static_cast<int64_t>(v / step); };
        uint64_t h = 1469598103934665603ull; // FNV-1a offset basis
        auto mixIn = [&](int64_t x) { h ^= static_cast<uint64_t>(x); h *= 1099511628211ull; };
        mixIn(quant(sunDir.x, 0.01f));
        mixIn(quant(sunDir.y, 0.01f));
        mixIn(quant(sunDir.z, 0.01f));
        mixIn(quant(timeOfDay, 0.02f));
        mixIn(quant(ambient, 0.01f));
        return h;
    }

    void RenderPassHandler::resetVolumetricFogComposite() { if (volumetricFogComposite) { volumetricFogComposite->cleanup(); volumetricFogComposite.reset(); } }
    void RenderPassHandler::initVolumetricFogComposite(volumetric::VolumetricPipeline* volPipeline)
    {
        if (volumetricFogComposite && volumetricFogComposite->isInitialized()) return;
        if (!volumetricFogComposite)
            volumetricFogComposite = std::make_unique<volumetric::VolumetricFogComposite>(device, swapChain, offscreenResources);
        volumetricFogComposite->init(volPipeline);
    }
    void RenderPassHandler::initSSGI() { if (ssgiPipeline && ssgiPipeline->isInitialized()) return; if (!ssgiPipeline) ssgiPipeline = std::make_unique<gi::SSGIPipeline>(device, swapChain, offscreenResources); ssgiPipeline->init(); }
    void RenderPassHandler::resetSSGI() { if (ssgiPipeline) { ssgiPipeline->cleanup(); ssgiPipeline.reset(); } }

    void RenderPassHandler::initSSR()
    {
        if (ssrPipeline && ssrPipeline->isInitialized()) return;
        if (!ssrPipeline) ssrPipeline = std::make_unique<ssr::SSRPipeline>(device, swapChain, offscreenResources);

        // Supply Hi-Z and normal+roughness resources from GPU-driven renderer
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            auto [hiZView, hiZSmplr] = gpuDrivenRenderer->getPrepassHiZViewSampler();
            if (hiZView && hiZSmplr)
                ssrPipeline->setHiZResources(hiZView, hiZSmplr);

            auto normalView = gpuDrivenRenderer->getPrepassNormalImageView();
            auto normalImg = gpuDrivenRenderer->getPrepassNormalImage();
            if (normalView && normalImg)
                ssrPipeline->setNormalRoughnessResources(normalView, normalImg);
        }

        ssrPipeline->init();
    }

    void RenderPassHandler::resetSSR()
    {
        if (ssrPipeline)
        {
            device.getLogicalDevice().waitIdle();
            ssrPipeline->cleanup();
            ssrPipeline.reset();
        }
    }

    void RenderPassHandler::applySSRSettings(const ::postprocess::SSRSettings& settings)
    {
        if (settings.enabled) initSSR();
        if (ssrPipeline)
        {
            bool halfResChanged = ssrPipeline->isInitialized() &&
                                  ssrPipeline->isHalfResolution() != settings.halfResolution;

            ssr::SSRSettings ssrSettings{};
            ssrSettings.enabled = settings.enabled;
            ssrSettings.maxDistance = settings.maxDistance;
            ssrSettings.intensity = settings.intensity;
            ssrSettings.roughnessThreshold = settings.roughnessThreshold;
            ssrSettings.edgeFadeStart = settings.edgeFadeStart;
            ssrSettings.temporalBlend = settings.temporalBlend;
            ssrSettings.maxSteps = settings.maxSteps;
            ssrSettings.halfResolution = settings.halfResolution;
            ssrPipeline->updateSettings(ssrSettings);

            if (halfResChanged)
            {
                device.getLogicalDevice().waitIdle();
                ssrPipeline->recreate();
            }
        }
    }

    void RenderPassHandler::initAtmosphere() { if (atmospherePipeline && atmospherePipeline->isInitialized()) return; if (!atmospherePipeline) atmospherePipeline = std::make_unique<atmosphere::AtmospherePipeline>(device, swapChain, offscreenResources); atmospherePipeline->init(); }
    void RenderPassHandler::resetAtmosphere() { if (atmospherePipeline) { atmospherePipeline->cleanup(); atmospherePipeline.reset(); } }
    void RenderPassHandler::applyAtmosphereSettings(const atmosphere::AtmosphereSettings& settings)
    {
        // Detect the dynamic-ambient mode edge BEFORE updating settings so we only rebuild the mesh
        // IBL descriptor once per toggle (never per-frame). (VK-1569)
        const bool wasDynamic = atmospherePipeline && atmospherePipeline->isInitialized() &&
                                atmospherePipeline->getSettings().enabled &&
                                atmospherePipeline->getSettings().dynamicAmbient;

        if (settings.enabled) initAtmosphere();
        if (atmospherePipeline) atmospherePipeline->updateSettings(settings);

        const bool nowDynamic = settings.enabled && settings.dynamicAmbient;
        if (wasDynamic == nowDynamic || !meshPipelineInitialized)
            return;

        if (nowDynamic)
        {
            // OFF -> ON: allocate + init the capture, do one blocking full capture so the first frame
            // samples valid ambient (no black pop), then bind the live maps once.
            if (!skyEnvCapture)
                skyEnvCapture = std::make_unique<atmosphere::SkyEnvironmentCapture>(device);
            if (!skyEnvCapture->isInitialized() && atmospherePipeline && atmospherePipeline->isInitialized())
                skyEnvCapture->init(device.getStagingCommandPool(), *atmospherePipeline);
            if (skyEnvCapture->isInitialized())
            {
                skyEnvCapture->captureBlocking(computeAmbientCaptureEpoch());
                reinitMeshPipelineWithDynamicAmbient();
            }
        }
        else
        {
            // ON -> OFF: revert to the scene's HDR IBL (VK-1574 capture) if one is loaded, else the
            // legacy static IBL, else the studio-gray default. When an HDR is present, bake it
            // blocking first so the live maps are valid the instant the mesh rebinds (no black pop).
            if (hdrEnvCapture && hdrEnvCapture->isInitialized() && hdrEnvCapture->hasSource())
            {
                hdrEnvCapture->captureBlocking(hdrEnvCapture->currentEpoch());
                reinitMeshPipelineWithHdrCapture();
                iblRenderer->initSkybox(hdrEnvCapture->getEnvLive());
            }
            else if (iblRenderer->isInitialized())
                reinitMeshPipelineWithIBL();
            else
                reinitMeshPipelineWithDefaults();
        }
    }

    void RenderPassHandler::initCloud()
    {
        if (cloudPipeline && cloudPipeline->isInitialized()) return;
        if (!cloudPipeline) cloudPipeline = std::make_unique<cloud::CloudPipeline>(device, swapChain, offscreenResources);
        if (atmospherePipeline) cloudPipeline->setAtmospherePipeline(atmospherePipeline.get());
        cloudPipeline->init();
    }
    void RenderPassHandler::resetCloud() { if (cloudPipeline) { cloudPipeline->cleanup(); cloudPipeline.reset(); } }
    void RenderPassHandler::applyCloudSettings(const cloud::CloudSettings& settings) { if (settings.enabled) initCloud(); if (cloudPipeline) cloudPipeline->updateSettings(settings); }
}
