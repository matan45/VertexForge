#include "GPUDrivenRenderer.hpp"
#include "../../core/Device.hpp" // VK-1620: getPhysicalDevice() for the world-height format check
#include "terrain/TerrainRVTManager.hpp"
#include "../virtualtexture/svt/SVTManager.hpp" // VK-1209: complete type for svtManager->setResidencyBudget
#include "terrain/TerrainRVTBaker.hpp"
#include "terrain/TerrainRVTLayout.hpp"
#include "terrain/TerrainRVTBudget.hpp" // VK-1610: pool geometry shared with the editor UI
#include "terrain/TerrainRVTCoverage.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "terrain/TerrainLayerVisibility.hpp" // VK-1613: per-layer visibility mask
#include "terrain/TerrainLayerPBRResolver.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include "resource/Types.hpp"
#include "asset/AssetRef.hpp"
#include "../../core/Texture.hpp"
#include "../../core/SwapChain.hpp"
#include "types/RenderSettings.hpp"
#include "stats/TerrainRVTStats.hpp" // VK-1610: residency/thrash readout for the profiler
#include "print/Log.hpp"
#include <chrono>
#include <limits>
#include <unordered_set>

namespace render::gpudriven
{
    void GPUDrivenRenderer::initTerrainSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                    const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
    {
        // VK-1620: resolve the world-height plane FIRST — the mesh buffer decides here whether to
        // create the 64 MB heightfield arena at all, and the baker/pool below must be built from the
        // same already-checked answer.
        syncTerrainRVTWorldHeight();

        terrain.meshBuffer = std::make_unique<TerrainMeshBuffer>(device);
        terrain.meshBuffer->init(terrain.rvtWorldHeight);

        terrain.adapter = std::make_unique<TerrainGPUAdapter>(*terrain.meshBuffer);
        terrain.streamManager = std::make_unique<TerrainStreamManager>(*terrain.meshBuffer, *terrain.adapter);

        if (terrain.pendingTileDataLoader)
        {
            terrain.streamManager->setTileDataLoader(std::move(terrain.pendingTileDataLoader));
            terrain.pendingTileDataLoader = nullptr;
        }
        if (terrain.pendingTileRAMEvictor)
        {
            terrain.streamManager->setTileRAMEvictor(std::move(terrain.pendingTileRAMEvictor));
            terrain.pendingTileRAMEvictor = nullptr;
        }
        if (terrain.pendingTileLoadContextProvider)
        {
            terrain.streamManager->setTileLoadContextProvider(std::move(terrain.pendingTileLoadContextProvider));
            terrain.pendingTileLoadContextProvider = nullptr;
        }

        terrain.pipeline = std::make_unique<TerrainMeshShaderPipeline>(device, swapChain);
        terrain.pipeline->setDetailMapsEnabled(terrain.detailMaps);
        // VK-1209: compile the terrain pipeline RVT-ready (set 5 + RVT_ENABLED) when config enabled it
        // before terrain init. Runtime toggling requires a pipeline recreate (restart-scoped, per config).
        if (vtCache.rvtEnabled)
            terrain.pipeline->setRVTSampleEnabled(true);
        terrain.pipeline->init(
            iblDescriptorSetLayout,
            bindlessTextures->getDescriptorSetLayout(),
            meshShaderPipeline->getMeshletDataLayout(),
            meshShaderPipeline->getVertexDataLayout(),
            lightBufferManager->getDescriptorSetLayout(),
            clusterGridManager->getDescriptorSetLayout(),
            lightCullingPipeline->getDescriptorSetLayout(),
            shadowSystem->getShadowDataLayout(),
            shadowSystem->getShadowTextureLayout(),
            colorFormats, depthFormat
        );

        shadowSystem->initTerrainShadowPass(
            terrain.pipeline->getTerrainDataLayout(),
            terrain.pipeline->getCachedMeshletLayout(),
            terrain.pipeline->getCachedVertexLayout()
        );

        // VK-1209: the RVT bake pipeline (self-contained; binds the terrain pipeline's own sets).
        if (vtCache.rvtEnabled)
        {
            const TerrainRVTLayout layout = terrainRVTLayout(terrain.detailMaps, terrain.rvtWorldHeight);
            terrainRVTBaker = std::make_unique<TerrainRVTBaker>(device);
            terrainRVTBaker->init(terrain.pipeline->getWeightMapLayout(),
                                  bindlessTextures->getDescriptorSetLayout(),
                                  terrain.pipeline->getTerrainDataLayout(),
                                  layout.planeFormats,
                                  terrain.pipeline->getCompositePermutation());
        }
    }

    void GPUDrivenRenderer::applyVirtualTextureSettings(const types::VirtualTextureSettings& settings)
    {
        const bool rvtToggled = vtCache.rvtEnabled != settings.rvtEnabled;
        const bool svtToggled = vtCache.svtEnabled != settings.svtEnabled;

        vtCache.rvtEnabled = settings.rvtEnabled;
        vtCache.svtEnabled = settings.svtEnabled;
        vtCache.rvtWorldHeight = settings.rvtWorldHeight;
        vtCache.rvtPoolBudgetMB = settings.rvtPoolBudgetMB;
        vtCache.svtPoolBudgetMB = settings.svtPoolBudgetMB;
        vtCache.rvtTexelsPerMeter = settings.rvtTexelsPerMeter;
        vtCache.pagesPerFrame = settings.pagesPerFrame;
        vtCache.evictionAgeFrames = settings.evictionAgeFrames;
        vtCache.svtPageLinearMaps = settings.svtPageLinearMaps; // VK-1480: 2nd (Unorm) SVT pool (restart)

        // Finding #10: page budget + eviction age apply live to already-running managers (the UI/docs
        // promise "apply live"). SVT clamps pagesPerFrame to its init-time staging-ring size internally;
        // terrain RVT applies both fully.
        if (svtManager)
            svtManager->setResidencyBudget(vtCache.pagesPerFrame, vtCache.evictionAgeFrames);
        if (terrainRVT)
            terrainRVT->setResidencyBudget(vtCache.pagesPerFrame, vtCache.evictionAgeFrames);

        // Runtime RVT toggle: rebuild the terrain pipeline so set 5 + RVT_ENABLED match the new
        // state, and create/tear down the RVT subsystems. The manager itself comes up on the next
        // updateTerrain (once world bounds are known) — which runs before the terrain draw — so the
        // set-5 descriptor is written before it is sampled. Pool byte budgets remain restart-scoped.
        if (rvtToggled && initialized && terrain.pipeline)
        {
            // Prevent a draw from using descriptors that refer to the manager views being replaced.
            terrain.pipeline->invalidateRVTSampleResources();
            terrain.pipeline->setRVTSampleEnabled(vtCache.rvtEnabled);
            recreateTerrainPipelineForRVT();

            if (vtCache.rvtEnabled)
            {
                // The RVT is being built fresh here, so this is a legitimate point to pick up a
                // changed world-height setting even though it is otherwise restart-scoped.
                syncTerrainRVTWorldHeight();
                if (!terrainRVTBaker)
                {
                    const TerrainRVTLayout layout = terrainRVTLayout(terrain.detailMaps, terrain.rvtWorldHeight);
                    terrainRVTBaker = std::make_unique<TerrainRVTBaker>(device);
                    terrainRVTBaker->init(terrain.pipeline->getWeightMapLayout(),
                                          bindlessTextures->getDescriptorSetLayout(),
                                          terrain.pipeline->getTerrainDataLayout(),
                                          layout.planeFormats,
                                          terrain.pipeline->getCompositePermutation());
                }
            }
            else
            {
                terrainRVT.reset();
                terrainRVTBaker.reset();
            }
            // VK-1620: the scene mesh pipelines hold the manager's page-table buffer in set-1
            // binding 8. Turning RVT off destroys it, so they must be recompiled without the macro
            // before anything records another draw.
            applyRVTBlendToggle();
        }

        // Runtime SVT toggle: rebuild the scene mesh pipelines (set-1 SVT bindings + SVT_ENABLED)
        // and create/tear down the SVT manager. Registration opt-in happens on the next material load.
        if (svtToggled && initialized)
            applySVTToggle();

        if (rvtToggled || svtToggled)
            vfLogInfo("VK-1209 virtual texturing: RVT={} SVT={} (rvtPool={}MB svtPool={}MB, {} pages/frame)",
                      vtCache.rvtEnabled, vtCache.svtEnabled,
                      vtCache.rvtPoolBudgetMB, vtCache.svtPoolBudgetMB, vtCache.pagesPerFrame);
    }

    void GPUDrivenRenderer::recreateTerrainPipelineForRVT()
    {
        if (!terrain.pipeline || !bindlessTextures || !meshShaderPipeline || !lightBufferManager ||
            !clusterGridManager || !lightCullingPipeline || !shadowSystem)
            return;

        // Same layouts + formats initTerrainSubsystems passed to init(); recreate() waits idle.
        terrain.pipeline->recreate(
            cachedIBLLayout,
            bindlessTextures->getDescriptorSetLayout(),
            meshShaderPipeline->getMeshletDataLayout(),
            meshShaderPipeline->getVertexDataLayout(),
            lightBufferManager->getDescriptorSetLayout(),
            clusterGridManager->getDescriptorSetLayout(),
            lightCullingPipeline->getDescriptorSetLayout(),
            shadowSystem->getShadowDataLayout(),
            shadowSystem->getShadowTextureLayout(),
            cachedColorFormats, cachedDepthFormat);
    }

    void GPUDrivenRenderer::syncTerrainCompositePermutation()
    {
        if (!initialized || !terrain.pipeline)
            return;

        // VK-1609. resolveTerrainLayerPBR already forced the contrast to 0 for every layer without
        // a real height source, so this is exactly "does any layer actually height-blend".
        bool anyHeightBlend = false;
        for (const auto& l : terrain.layerData)
        {
            if (l.heightBlendContrast > 0.0f)
            {
                anyHeightBlend = true;
                break;
            }
        }

        // Every setter must run before the short-circuit — writing `changed || setter(...)` would
        // skip the later setters as soon as one flag moved, silently leaving the pipeline on a
        // stale permutation. Separate statements, deliberately, rather than a chain of ||.
        bool changed = terrain.pipeline->setHeightBlendEnabled(anyHeightBlend);
        // VK-1611. Material-global rather than per-layer, and derived from the RESOLVED (clamped)
        // scalars so a knee/width the artist typed out of range cannot flip the permutation.
        const bool wantsRescale = terrainMaterialWantsDistanceRescale(terrain.antiTiling);
        if (terrain.pipeline->setDistanceRescaleEnabled(wantsRescale))
            changed = true;
        if (terrain.pipeline->setMacroVariationEnabled(
                terrainMaterialWantsMacroVariation(terrain.antiTiling)))
            changed = true;
        // VK-1612. Like the height-blend flag, derived from the uploaded per-layer scalars, so a
        // layer that opted in without an albedo texture cannot switch the permutation on.
        bool anyHexTiling = false;
        for (const auto& l : terrain.layerData)
        {
            if (l.hexTilingStrength > 0.0f)
            {
                anyHexTiling = true;
                break;
            }
        }
        if (terrain.pipeline->setHexTilingEnabled(anyHexTiling))
            changed = true;

        // VK-1614. Tracked SEPARATELY from `changed`: these three macros gate code that runs after the
        // RVT-resolve / live-composite join, so they cannot alter a single byte the bake writes.
        // Folding them into `changed` would tear down the baker and invalidate every resident page
        // every time an artist ticked "weather response" on a layer — a full RVT re-bake for a change
        // the bake cannot see.
        bool liveOnlyChanged = false;
        // Derived from the uploaded per-layer scalars, exactly like the two loops above: a layer that
        // did not opt in resolved to 0 and cannot switch the permutation on.
        bool anyWeatherResponse = false;
        for (const auto& l : terrain.layerData)
        {
            if (l.layerPorosity > 0.0f || l.layerSnowRetention > 0.0f)
            {
                anyWeatherResponse = true;
                break;
            }
        }
        if (terrain.pipeline->setWeatherResponseEnabled(anyWeatherResponse))
            liveOnlyChanged = true;
        // Puddles need somewhere for water to come from, so they ride the same content signal as the
        // response plus an assigned mask. With neither, the term would be multiplied by a wetness that
        // only the global weather can raise — which the plain wetness response already handles.
        if (terrain.pipeline->setPuddlesEnabled(anyWeatherResponse || terrain.surfaceMaskAssigned))
            liveOnlyChanged = true;
        if (terrain.pipeline->setSurfaceMaskEnabled(terrain.surfaceMaskAssigned))
            liveOnlyChanged = true;
        // VK-1625 POM-lite. Live-only on the same terms, and derived from the RESOLVED depth so a
        // value the artist typed out of range cannot flip the permutation. It joins liveOnlyChanged
        // rather than `changed` because the bake shader never sees TERRAIN_PARALLAX — the offset is
        // applied at final shading to both sampling paths — so dragging the depth slider must not
        // invalidate a single resident page.
        if (terrain.pipeline->setParallaxEnabled(terrainMaterialWantsParallax(terrain.parallax)))
            liveOnlyChanged = true;

        if (!changed && !liveOnlyChanged)
            return; // unchanged - no recompile

        recreateTerrainPipelineForRVT(); // performs the single device-idle wait for this transition
        // recreate() rebuilds only the pipeline/layout/shader — the descriptor set survives — but a
        // mask assign in the same tick is what flipped surfaceMaskAssigned above, so bindings 5/6 may
        // be pointing at the dummy. No-op unless the owner reports a resource change.
        terrain.pipeline->syncSurfaceMaskDescriptor();

        // The bake shader #includes the same generated composite, so it must be recompiled with the
        // matching macros. init() early-returns once its pipeline exists, hence the full reset. Every
        // resident page was baked with the old composite, so they all have to go.
        // Guarded on `changed`, NOT on liveOnlyChanged: see the note above.
        if (changed && vtCache.rvtEnabled && terrainRVTBaker && bindlessTextures)
        {
            const TerrainRVTLayout layout = terrainRVTLayout(terrain.detailMaps, terrain.rvtWorldHeight);
            terrainRVTBaker.reset();
            terrainRVTBaker = std::make_unique<TerrainRVTBaker>(device);
            terrainRVTBaker->init(terrain.pipeline->getWeightMapLayout(),
                                  bindlessTextures->getDescriptorSetLayout(),
                                  terrain.pipeline->getTerrainDataLayout(),
                                  layout.planeFormats,
                                  terrain.pipeline->getCompositePermutation());
            rvtInvalidateAll = true;
        }

        vfLogInfo("Terrain composite permutation: {}",
                  describeTerrainCompositePermutation(terrain.pipeline->getCompositePermutation()));
    }

    bool GPUDrivenRenderer::isTerrainRVTActive() const
    {
        return terrainRVT != nullptr && terrainRVT->isInitialized();
    }

    void GPUDrivenRenderer::updateTerrainRVTResidency()
    {
        // The caller gates on isTerrainRVTActive() and clears the VK-1610 residency readout when
        // there is no manager, so this only guards against a direct call.
        if (!terrainRVT)
            return;
        terrainRVT->markFeedbackReady();   // the prior frame's copy has completed (fence-gated caller)
        terrainRVT->beginFrameReadback();  // decode requested pages

        // Coverage gate: only make a page resident if its border-expanded world rect overlaps a
        // loaded terrain tile, mirroring bakeTerrainRVT's per-tile overlap so gate and bake agree.
        // Uncovered pages stay non-resident and render via the composite fallback, so the per-frame
        // page budget and physical tiles are spent on pages that actually carry detail.
        const std::vector<TerrainTileGPUData>& tiles = terrain.tileData;
        const float borderFrac =
            static_cast<float>(vt::VT_BORDER) / static_cast<float>(vt::VT_PAGE_INTERIOR);
        // Reuse the shared, unit-tested coverage helper (TerrainRVTCoverage.hpp) instead of open-coding
        // the overlap loop; build the tile rects once per frame, not once per page.
        std::vector<TerrainCoverageRect> coverageRects;
        coverageRects.reserve(tiles.size());
        for (const auto& t : tiles)
            coverageRects.push_back({t.aabbMin.x, t.aabbMin.z, t.aabbMax.x, t.aabbMax.z});
        auto covered = [&coverageRects, borderFrac](const glm::vec4& rect) -> bool
        {
            const glm::vec2 pageMin(rect.x, rect.y);
            const glm::vec2 pageSize(rect.z, rect.w);
            const glm::vec2 margin = pageSize * borderFrac;
            const TerrainCoverageRect q{
                pageMin.x - margin.x, pageMin.y - margin.y,
                pageMin.x + pageSize.x + margin.x, pageMin.y + pageSize.y + margin.y};
            return terrainRectCovered(q, coverageRects, /*requireFull*/ false);
        };
        terrainRVT->updateResidency(rvtFrameCounter++, covered);

        // VK-1610: publish residency for the profiler. Read straight after updateResidency so the
        // counters describe the frame that was just planned, not a mix of two.
        const auto& cpu = terrainRVT->lastCpuStats();
        const auto geo = ::terrain::terrainRVTPoolGeometry(vtCache.rvtPoolBudgetMB, terrain.detailMaps,
                                                           terrain.rvtWorldHeight);
        TerrainRVTFrameStats out;
        out.active = terrainRVT->isInitialized();
        out.poolDim = geo.poolDim;
        out.planeCount = geo.planeCount;
        out.bytesPerTexel = geo.bytesPerTexel;
        out.budgetMB = vtCache.rvtPoolBudgetMB;
        // The pool is the authority on capacity; geo only mirrors the math that sized it.
        out.capacityPages = terrainRVT->getPool() ? terrainRVT->getPool()->maxTiles() : geo.capacityPages;
        out.residentPages = terrainRVT->residentPageCount();
        out.requestedPages = cpu.requestedPages;
        out.allocatedPages = cpu.allocatedPages;
        out.evictedPages = cpu.evictedPages;
        out.scheduledBakes = terrainRVT->bakesThisFrame();
        out.uncoveredSkipped = cpu.uncoveredSkipped;
        out.unmetPages = cpu.unmetPages;
        out.budgetLimited = cpu.budgetLimited;
        out.poolLimited = cpu.poolLimited;
        out.readbackUs = cpu.readbackUs;
        out.decodeUs = cpu.decodeUs;
        out.residencyUs = cpu.residencyUs;
        TerrainRVTStats::instance().publish(out);
    }

    void GPUDrivenRenderer::bakeTerrainRVT(vk::CommandBuffer cmd)
    {
        if (!terrainRVT || !terrainRVTBaker || !terrainRVTBaker->isReady())
            return;

        terrainRVT->clearFeedback(cmd); // fresh feedback for this frame's terrain draw
        terrainRVT->uploadPageTable(cmd);

        const float texScale = terrain.textureScale > 0.0f ? terrain.textureScale : 0.1f;
        const std::vector<TerrainTileGPUData>& tiles = terrain.tileData;
        TerrainRVTBaker* baker = terrainRVTBaker.get();
        const vt::VTPhysicalPool* pool = terrainRVT->getPool();

        // recordBakes transitions the pool ShaderRead<->ColorAttachment around this callback.
        terrainRVT->recordBakes(cmd,
            [&](vk::CommandBuffer c, const std::vector<TerrainRVTManager::ScheduledBake>& bakes)
            {
                baker->begin(c, *pool,
                             terrain.pipeline->getWeightMapDescriptorSet(),
                             bindlessTextures->getDescriptorSet(),
                             terrain.pipeline->getTerrainDataDescriptorSet());

                const float borderFrac = static_cast<float>(vt::VT_BORDER) / static_cast<float>(vt::VT_PAGE_INTERIOR);

                for (const auto& b : bakes)
                {
                    baker->beginPage(c, *pool, b.tile);

                    // Expand the core page rect by the border margin so the full 128-texel tile
                    // (incl. its 4-texel border) samples correct neighbour content.
                    const glm::vec2 pageMin(b.worldRect.x, b.worldRect.y);
                    const glm::vec2 pageSize(b.worldRect.z, b.worldRect.w);
                    const glm::vec2 margin = pageSize * borderFrac;
                    const glm::vec2 expMin = pageMin - margin;
                    const glm::vec2 expSize = pageSize + margin * 2.0f;
                    const glm::vec2 expMax = expMin + expSize;

                    for (uint32_t ti = 0; ti < tiles.size(); ++ti)
                    {
                        const TerrainTileGPUData& t = tiles[ti];
                        const glm::vec2 tMin(t.aabbMin.x, t.aabbMin.z);
                        const glm::vec2 tMax(t.aabbMax.x, t.aabbMax.z);
                        const glm::vec2 qMin = glm::max(expMin, tMin);
                        const glm::vec2 qMax = glm::min(expMax, tMax);
                        if (qMin.x >= qMax.x || qMin.y >= qMax.y)
                            continue; // no overlap

                        TerrainRVTBaker::TilePush pc;
                        pc.pageWorldMin = expMin;
                        pc.pageWorldSize = expSize;
                        pc.quadWorldMin = qMin;
                        pc.quadWorldSize = qMax - qMin;
                        pc.tileWorldMin = tMin;
                        pc.tileWorldSize = glm::max(tMax.x - tMin.x, 1.0f);
                        pc.fragTileIndex = ti;
                        pc.textureScale = texScale;
                        // VK-1620. Unconditional: the fields are ignored unless the bake compiled
                        // TERRAIN_RVT_WORLD_HEIGHT, and branching here would only risk them going
                        // stale when the plane is toggled.
                        pc.heightMin = rvtHeightMin;
                        pc.invHeightRange = 1.0f / rvtHeightRange;
                        baker->drawTile(c, pc);
                    }
                }
                baker->end(c);
            });
    }

    void GPUDrivenRenderer::copyTerrainRVTFeedback(vk::CommandBuffer cmd)
    {
        if (!terrainRVT)
            return;
        terrainRVT->copyFeedbackToStaging(cmd);
    }

    void GPUDrivenRenderer::setTerrainFrustumCullingEnabled(bool enabled)
    {
        if (terrain.pipeline)
        {
            terrain.pipeline->setFrustumCullingEnabled(enabled);
        }
    }

    void GPUDrivenRenderer::setTerrainMeshletCullingEnabled(bool enabled)
    {
        if (terrain.pipeline)
        {
            terrain.pipeline->setMeshletCullingEnabled(enabled);
        }
    }

    void GPUDrivenRenderer::setTerrainCastShadows(bool cast)
    {
        if (terrain.castShadows == cast)
            return;
        terrain.castShadows = cast;
        // Cached VSM pages still hold depth baked with the previous caster set —
        // mark every page dirty so they re-render without/with terrain.
        if (shadowSystem)
            shadowSystem->notifySceneChanged();
    }

    void GPUDrivenRenderer::setTerrainDetailMaps(bool allowed)
    {
        if (terrain.detailMapsAllowed == allowed)
            return;

        terrain.detailMapsAllowed = allowed;

        // VK-1610: the effective permutation is derived from the terrain material, so this setter
        // does NOT decide it. Re-resolving the loaded terrain both re-derives the flag and
        // registers (on) or drops (off) the per-layer normal/emission slots to match, which keeps
        // one code path instead of two that can disagree.
        if (initialized && bindlessTextures && materials.textureCache && !terrain.currentMaterialPath.empty())
        {
            invalidateTerrainLayerData();
            registerTerrainLayerTextures(terrain.currentMaterialPath);
            return;
        }

        // No material to derive from - which also means no detail content, in either direction.
        // Settings routinely arrive before a terrain exists; the first material load re-derives.
        syncTerrainDetailMapsPermutation(false);
    }

    void GPUDrivenRenderer::setTerrainSurfaceMask(uint32_t width, uint32_t height,
                                                  const std::vector<uint8_t>& rgba,
                                                  const glm::vec4& worldRect,
                                                  float wetnessScale, float snowScale)
    {
        if (!initialized || !terrain.pipeline)
            return;

        if (width == 0 || height == 0 || rgba.empty())
        {
            clearTerrainSurfaceMask();
            return;
        }

        auto& mask = terrain.pipeline->getSurfaceMask();
        if (!mask.createMask(width, height))
            return;

        mask.queueUpload(rgba);
        mask.setParams(worldRect.x, worldRect.y, worldRect.z, worldRect.w,
                       wetnessScale, snowScale, true);
        terrain.pipeline->syncSurfaceMaskDescriptor();

        // The macro is resource-derived, so flipping this is what compiles the sampler in. The sync
        // treats it as live-only and therefore skips the RVT baker teardown.
        terrain.surfaceMaskAssigned = true;
        syncTerrainCompositePermutation();
    }

    void GPUDrivenRenderer::updateTerrainSurfaceMaskPixels(const std::vector<uint8_t>& rgba)
    {
        if (!initialized || !terrain.pipeline || !terrain.surfaceMaskAssigned)
            return;

        // Paint-stroke path: pixels only. No descriptor rewrite, no permutation sync, no pipeline
        // recreate — the image object is unchanged, only its contents.
        terrain.pipeline->getSurfaceMask().queueUpload(rgba);
    }

    void GPUDrivenRenderer::clearTerrainSurfaceMask()
    {
        if (!initialized || !terrain.pipeline || !terrain.surfaceMaskAssigned)
            return;

        auto& mask = terrain.pipeline->getSurfaceMask();
        mask.releaseMask();
        mask.setParams(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, false);
        // Point bindings 5/6 back at the 1x1 dummy BEFORE the macro is dropped, so the set is never
        // left holding a destroyed view (this layout has no ePartiallyBound).
        terrain.pipeline->syncSurfaceMaskDescriptor();

        terrain.surfaceMaskAssigned = false;
        syncTerrainCompositePermutation();
    }

    void GPUDrivenRenderer::flushTerrainSurfaceMaskUploads(const vk::CommandBuffer& cmd)
    {
        if (!initialized || !terrain.pipeline)
            return;

        terrain.pipeline->flushSurfaceMaskUploads(cmd);
    }

    void GPUDrivenRenderer::syncTerrainRVTWorldHeight()
    {
        const bool requested = vtCache.rvtWorldHeight;
        bool effective = requested;

        // R16_UNORM is not one of Vulkan's mandatory colour-attachment formats. Every desktop GPU
        // this engine targets supports it, but VTPhysicalPool creates its planes with no capability
        // check, so an unsupported device would fail image creation deep inside pool init with no
        // hint as to why. Fail the FEATURE instead, loudly, and leave the rest of the RVT working.
        if (requested && !terrainRVTWorldHeightSupported(device.getPhysicalDevice()))
        {
            vfLogError("VK-1620 terrain RVT world-height plane requested, but this device cannot use "
                       "R16_UNORM as a colour attachment - mesh-into-terrain blending is disabled.");
            effective = false;
        }

        if (terrain.rvtWorldHeight == effective)
            return;

        terrain.rvtWorldHeight = effective;

        // Same reasoning as the detail-maps log: the page count is what decides whether the camera
        // thrashes, and this flag moves it without the MB budget changing.
        const auto geo = ::terrain::terrainRVTPoolGeometry(vtCache.rvtPoolBudgetMB, terrain.detailMaps,
                                                           effective);
        vfLogInfo("VK-1620 terrain RVT world-height plane: {}; RVT pool {}x{} = {} pages at {} B/texel "
                  "from a {} MB budget",
                  effective ? "ON" : "OFF", geo.poolDim, geo.poolDim, geo.capacityPages,
                  geo.bytesPerTexel, vtCache.rvtPoolBudgetMB);

        if (effective && geo.capacityPages < ::terrain::TERRAIN_RVT_MIN_HEALTHY_PAGES)
            vfLogWarning("VK-1620: the world-height plane takes this RVT pool to {} pages, below the {} "
                         "a camera needs to keep its footprint resident. Raise the RVT pool budget.",
                         geo.capacityPages, ::terrain::TERRAIN_RVT_MIN_HEALTHY_PAGES);
    }

    void GPUDrivenRenderer::syncTerrainDetailMapsPermutation(bool effective)
    {
        if (terrain.detailMaps == effective)
            return;

        // Cached for initTerrainSubsystems, which consumes it when it builds both the terrain
        // pipeline permutation and the RVT baker.
        terrain.detailMaps = effective;
        if (!initialized || !terrain.pipeline)
            return;

        // recreate() performs the single device-idle wait for this transition. Mark the old set
        // unready before that wait so no subsequent recording can sample manager-owned views while
        // they are being replaced.
        terrain.pipeline->invalidateRVTSampleResources();
        terrain.pipeline->setDetailMapsEnabled(effective);
        recreateTerrainPipelineForRVT();

        if (vtCache.rvtEnabled)
        {
            // Unlike VK-1609's height-blend flag, this one changes the RVT PLANE LAYOUT (2 planes
            // at 8 B/texel vs 4 at 20), so the pool images themselves are wrong, not just the bake
            // shader - the manager has to go too. updateTerrain re-creates it next frame from
            // terrain.detailMaps, which is why this assignment happens before any early return.
            terrainRVT.reset();
            terrainRVTBaker.reset();
            // VK-1620: same hazard as the RVT toggle — the scene mesh pipelines are holding this
            // manager's page table and its atlas views' bindless slots. updateTerrain re-creates the
            // manager next frame and re-wires them; this turns the macro off until it does.
            applyRVTBlendToggle();

            // Deliberately NOT guarded on `bindlessTextures`: reaching here means `initialized`,
            // which cannot be true without it. A guard would look defensive but would leave the
            // baker permanently null, and syncTerrainHeightBlendPermutation's `terrainRVTBaker &&`
            // precondition would then never rebuild it - RVT would silently stop baking for the
            // rest of the session.
            const TerrainRVTLayout layout = terrainRVTLayout(effective, terrain.rvtWorldHeight);
            terrainRVTBaker = std::make_unique<TerrainRVTBaker>(device);
            terrainRVTBaker->init(terrain.pipeline->getWeightMapLayout(),
                                  bindlessTextures->getDescriptorSetLayout(),
                                  terrain.pipeline->getTerrainDataLayout(),
                                  layout.planeFormats,
                                  terrain.pipeline->getCompositePermutation());
            rvtFrameCounter = 0;
            // A brand-new pool holds no pages, so there is nothing left to invalidate.
            rvtInvalidateAll = false;

            // The page count is what decides whether the camera thrashes, and it moves with the
            // plane layout even though the MB budget did not - so say it out loud.
            const auto geo = ::terrain::terrainRVTPoolGeometry(vtCache.rvtPoolBudgetMB, effective,
                                                               terrain.rvtWorldHeight);
            vfLogInfo("VK-1610 terrain detail maps: {} (setting allows: {}); RVT pool {}x{} = {} pages "
                      "at {} B/texel from a {} MB budget",
                      effective ? "ON" : "OFF", terrain.detailMapsAllowed ? "yes" : "no",
                      geo.poolDim, geo.poolDim, geo.capacityPages, geo.bytesPerTexel,
                      vtCache.rvtPoolBudgetMB);
        }
        else
        {
            vfLogInfo("VK-1610 terrain detail maps: {} (setting allows: {})",
                      effective ? "ON" : "OFF", terrain.detailMapsAllowed ? "yes" : "no");
        }
    }

    void GPUDrivenRenderer::registerTerrainLayerTextures(const std::string& materialPath)
    {
        if (materialPath.empty())
        {
            return;
        }

        // VK-1486: a referenced .vfMat / .vfMatInstance may have changed even when the terrain
        // material path and layer data are unchanged; consume the flag to force a re-resolve.
        const bool matSourceDirty = terrain.materialSourceDirty.exchange(false, std::memory_order_relaxed);
        if (materialPath == terrain.currentMaterialPath && !terrain.layerDataDirty && !matSourceDirty)
        {
            return;
        }

        if (!bindlessTextures || !materials.textureCache)
        {
            return;
        }

        auto materialData = resource::ResourceManager::loadTerrainMaterial(asset::AssetRef::fromPath(materialPath));
        if (!materialData)
        {
            return;
        }

        terrain.layerData.clear();
        terrain.layerData.resize(materialData->activeLayerCount);

        auto tryRegisterLayerTex = [&](const std::string& texPath, vk::Format format = vk::Format::eR8G8B8A8Unorm) -> uint32_t
        {
            if (texPath.empty()) return 0;
            if (!materials.textureCache->loadTexture(texPath, format)) return 0;
            vk::ImageView view = materials.textureCache->getViewForPath(texPath);
            vk::Sampler sampler = materials.textureCache->getSamplerForPath(texPath);
            if (!view || !sampler) return 0;
            return bindlessTextures->registerTexture(texPath, view, sampler);
        };

        // VK-1486: warn at most once per source material that carries PBR data terrain cannot represent.
        static std::unordered_set<std::string> warnedUnrepresentableMaterials;

        std::vector<ResolvedTerrainLayerPBR> resolvedLayers(materialData->activeLayerCount);

        for (uint8_t i = 0; i < materialData->activeLayerCount; ++i)
        {
            const auto& layer = materialData->layers[i];
            TerrainLayerGPUData& gpuLayer = terrain.layerData[i];
            gpuLayer = {};

            // VK-1486: optionally source terrain-supported PBR fields from a .vfMat / .vfMatInstance.
            mesh::ExtractedPBRValues pbr;
            const mesh::ExtractedPBRValues* pbrPtr = nullptr;
            if (layer.materialRef.isValid())
            {
                const std::string matPath = layer.materialRef.resolve();
                pbr = mesh::MaterialPBRExtractor::extractPBRFromPath(matPath);
                if (!pbr.materialPath.empty()) // materialPath is set only on successful extraction
                {
                    pbrPtr = &pbr;

                    // Terrain has a single packed-ORM slot and no albedo-tint field; warn once if the
                    // material relies on PBR data terrain drops.
                    const bool separateOrmMaps = !pbr.usesORM()
                        && (!pbr.metallicTexturePath.empty() || !pbr.roughnessTexturePath.empty() || !pbr.aoTexturePath.empty());
                    const bool droppedAlbedoTint = pbr.albedoTexturePath.empty()
                        && (pbr.albedo.r != 1.0f || pbr.albedo.g != 1.0f || pbr.albedo.b != 1.0f);
                    // VK-1609: terrain reads per-layer height from ORM alpha, so a standalone Height
                    // slot is dropped, and selecting Height Blend without any ORM is a silent no-op
                    // (resolveTerrainLayerPBR forces the contrast to 0). Both are actionable for the
                    // artist, and both are free to detect from data already extracted above.
                    const bool droppedHeightMap = !pbr.heightTexturePath.empty();
                    const bool heightBlendNoOrm =
                        layer.blendMode == terrain::TerrainLayerBlendMode::HeightBlend && !pbr.usesORM();
                    if ((separateOrmMaps || droppedAlbedoTint || droppedHeightMap || heightBlendNoOrm)
                        && warnedUnrepresentableMaterials.insert(matPath).second)
                    {
                        std::string dropped;
                        auto addDropped = [&dropped](const char* what)
                        {
                            if (!dropped.empty()) dropped += " + ";
                            dropped += what;
                        };
                        if (separateOrmMaps) addDropped("separate metallic/roughness/AO maps");
                        if (droppedAlbedoTint) addDropped("albedo tint");
                        if (droppedHeightMap) addDropped("standalone height map (terrain reads height "
                                                         "from ORM alpha - repack it into the ORM)");
                        if (heightBlendNoOrm) addDropped("height blend selected but the material has no "
                                                         "packed ORM, so this layer blends linearly");
                        vfLogWarning("GPUDrivenRenderer: terrain material source '{}' uses PBR data terrain cannot "
                                     "represent ({}); falling back to packed ORM / scalars.",
                                     matPath, dropped);
                    }
                }
                else
                {
                    vfLogWarning("GPUDrivenRenderer: terrain layer {} material '{}' failed to load; "
                                 "rendering with defaults", i, matPath);
                }
            }

            ResolvedTerrainLayerPBR& r = resolvedLayers[i];
            r = resolveTerrainLayerPBR(layer, pbrPtr);
        }

        // VK-1610. Resolving every layer first is what makes this possible: the detail-maps
        // permutation is derived from the material, and it has to be settled BEFORE the
        // registration loop below, because that loop's dead-VRAM gate reads it.
        syncTerrainDetailMapsPermutation(terrain.detailMapsAllowed
                                         && terrainMaterialWantsDetailMaps(resolvedLayers));

        for (uint8_t i = 0; i < materialData->activeLayerCount; ++i)
        {
            const ResolvedTerrainLayerPBR& r = resolvedLayers[i];
            TerrainLayerGPUData& gpuLayer = terrain.layerData[i];

            gpuLayer.albedoTextureIndex = tryRegisterLayerTex(r.albedoPath, vk::Format::eR8G8B8A8Srgb);
            // Normal + emission textures are sampled only by the detail-maps shader permutation
            // (the non-detail composite drops the normal fetch and uses scalar emission), so skip
            // uploading them to VRAM / the bindless table when detail maps are off. The sync above
            // has already settled the effective flag for exactly this material, so these register
            // or drop together with the permutation that samples them.
            gpuLayer.normalTextureIndex = terrain.detailMaps ? tryRegisterLayerTex(r.normalPath) : 0;
            gpuLayer.ormTextureIndex = tryRegisterLayerTex(r.ormPath);
            gpuLayer.emissionTextureIndex = terrain.detailMaps
                ? tryRegisterLayerTex(r.emissionPath, vk::Format::eR8G8B8A8Srgb)
                : 0;

            gpuLayer.tilingScale = r.tilingScale;
            gpuLayer.roughness = r.roughness;
            gpuLayer.metallic = r.metallic;
            gpuLayer.ao = r.ao;
            gpuLayer.emissionStrength = r.emissionStrength;
            // VK-1609. Exactly 0 for every layer with no height source, which is what keeps such a
            // layer bit-identical to the pre-VK-1609 composite even inside the height permutation.
            gpuLayer.heightBlendContrast = r.heightBlendContrast;
            // VK-1612. Exactly 0 for every layer that did not opt in or has no albedo texture,
            // which is what makes such a layer take the single-tap path inside the hex permutation.
            gpuLayer.hexTilingStrength = r.hexTilingStrength;
            gpuLayer.hexCellScale = r.hexCellScale;
            gpuLayer.hexContrast = r.hexContrast;
            gpuLayer.hexRotationStrength = r.hexRotationStrength;
            // VK-1614. Exactly 0 for every layer that did not opt into a weather response — the
            // sentinel the shader's authority accumulator resolves back to the derived porosity /
            // full snow retention, so such a layer renders bit-identically to the pre-VK-1614
            // shader even inside the weather permutation. Slots past activeLayerCount keep the
            // value-initialized 0 from `gpuLayer = {}` and therefore read neutral too, which
            // matters because palette indices are allowed to point at them.
            gpuLayer.layerPorosity = r.porosity;
            gpuLayer.layerSnowRetention = r.snowRetention;
        }

        {
            // VK-1486: track the resolved (material-sourced or manual) texture paths so edits to any
            // of them hot-reload the terrain.
            std::vector<std::string> texPaths;
            texPaths.reserve(static_cast<size_t>(materialData->activeLayerCount) * 4);
            for (const auto& r : resolvedLayers)
            {
                texPaths.push_back(r.albedoPath);
                texPaths.push_back(r.normalPath);
                texPaths.push_back(r.ormPath);
                texPaths.push_back(r.emissionPath);
            }
            registerTextureDependencies(materialPath, texPaths);
        }

        // VK-1611. Material-global, so it is resolved straight from the asset rather than from the
        // per-layer loop. Uploaded before the permutation sync below, which derives the
        // distance-rescale macro from these same clamped values.
        terrain.antiTiling = resolveTerrainAntiTiling(materialData->antiTiling);
        // VK-1625. Material-global on the same terms, and likewise resolved before the permutation
        // sync below, which reads terrain.parallax.depthMetres to decide whether TERRAIN_PARALLAX is
        // compiled at all. Note where it does NOT go: the anti-tiling scalars live on the weight-map
        // set precisely so the RVT bake can read them, and these must never be reachable from there.
        terrain.parallax = resolveTerrainParallax(materialData->parallax);

        if (terrain.pipeline)
        {
            terrain.pipeline->updateTerrainLayerInfo(terrain.layerData);
            terrain.pipeline->updateTerrainAntiTiling(terrain.antiTiling);
            terrain.pipeline->setParallaxParams(terrain.parallax);
        }

        // VK-1613. Per-layer `enabled` is honoured on the WEIGHT side, not in TerrainLayerGPUData:
        // the adapter zeroes a hidden layer's weight bytes as it packs them, which the composite
        // already treats as "this channel is not here". Pushing the mask from here means it lands
        // before this frame's weight-upload pass (streamManager->update, further down updateTerrain)
        // and inherits the RVT invalidate at the end of this function, so baked pages and the live
        // composite cannot disagree about which layers exist.
        //
        // Texture registration above deliberately does NOT skip hidden layers, unlike the
        // detail-maps VRAM gate. The weight re-pack lands a frame later than the mask, and a layer
        // whose albedo slot had been dropped would composite as flat vec3(0.5) grey in between;
        // keeping the slots also makes unhiding instant instead of a texture reload.
        if (terrain.adapter)
        {
            terrain.adapter->setLayerEnabledMask(terrain::buildLayerEnabledMask(*materialData));
        }

        // VK-1609/VK-1611. Runs at the END, not next to the detail sync above, because it derives
        // the height flag from terrain.layerData - which only exists once the registration loop has
        // filled it. The detail permutation is the mirror image: it has to be settled BEFORE that
        // loop, because it decides what the loop registers. Neither re-enters this function.
        //
        // Height blend and distance rescale share ONE sync (and therefore one pipeline rebuild)
        // precisely because neither changes what the loop registers. Detail maps cannot join them:
        // it also changes the RVT PLANE LAYOUT, so it has to tear down the manager, not just the
        // baker.
        //
        // VK-1610 consequence, unchanged: a material change that flips the detail flag as well
        // rebuilds the RVT baker twice - once above with the previous composite macros, once here
        // with the correct ones. That is deliberate. Collapsing it would mean leaving the baker
        // stale between the two syncs, and a baker whose macros disagree with the live pipeline is
        // exactly the failure VK-1609 warns about (baked pages and the live composite diverge at
        // page-residency boundaries, invisible with RVT off). The cost is one extra pipeline build
        // per material load, never per frame.
        syncTerrainCompositePermutation();

        terrain.currentMaterialPath = materialPath;
        terrain.layerDataDirty = false;
        rvtInvalidateAll = true; // VK-1209: terrain material changed -> re-bake resident RVT pages
        vfLogInfo("GPUDrivenRenderer: Registered {} terrain layer textures from '{}'",
                   materialData->activeLayerCount, materialPath);
    }

    void GPUDrivenRenderer::updateTerrain(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                          const glm::vec3& cameraPosition,
                                          const std::string& terrainMaterialPath,
                                          const glm::vec2& terrainGridWorldMin,
                                          const glm::vec2& terrainGridWorldMax)
    {
        auto frameStart = std::chrono::high_resolution_clock::now();

        if (!initialized || !terrain.renderingEnabled || !terrain.adapter || !terrain.pipeline)
        {
            return;
        }

        if (!terrainMaterialPath.empty())
        {
            registerTerrainLayerTextures(terrainMaterialPath);
        }
        else if (terrain.layerData.empty())
        {
            TerrainLayerGPUData defaultLayer{};
            defaultLayer.albedoTextureIndex = 0;
            defaultLayer.normalTextureIndex = 0;
            defaultLayer.ormTextureIndex = 0;
            defaultLayer.tilingScale = 1.0f;
            defaultLayer.roughness = 0.8f;
            defaultLayer.metallic = 0.0f;
            defaultLayer.ao = 1.0f;
            defaultLayer.emissionStrength = 0.0f;
            defaultLayer.emissionTextureIndex = 0;
            terrain.layerData.push_back(defaultLayer);

            if (terrain.pipeline)
            {
                terrain.pipeline->updateTerrainLayerInfo(terrain.layerData);
            }
        }

        if (visibleTiles.empty())
        {
            terrain.tileData.clear();
            return;
        }

        auto streamStart = std::chrono::high_resolution_clock::now();

        if (terrain.streamManager)
        {
            terrain.streamManager->update(visibleTiles, cameraPosition);
        }
        else
        {
            for (terrain::TerrainTile* tile : visibleTiles)
            {
                if (!tile || !tile->isVisible)
                {
                    continue;
                }

                TerrainTileKey key{tile->coord.x, tile->coord.z};
                if (!terrain.adapter->hasTile(key))
                {
                    terrain.adapter->uploadTile(*tile);
                }
            }
        }

        auto streamEnd = std::chrono::high_resolution_clock::now();
        terrain.streamingUs = std::chrono::duration<float, std::micro>(streamEnd - streamStart).count();

        terrain.adapter->markGPUTileDataDirty();

        auto buildStart = std::chrono::high_resolution_clock::now();
        const auto& newTileData = terrain.adapter->buildGPUTileData(visibleTiles);
        auto buildEnd = std::chrono::high_resolution_clock::now();
        terrain.buildTileDataUs = std::chrono::duration<float, std::micro>(buildEnd - buildStart).count();

        auto uploadStart = std::chrono::high_resolution_clock::now();

        if (!newTileData.empty())
        {
            terrain.tileData = newTileData;
            terrain.pipeline->updateTileData(terrain.tileData);
        }
        else
        {
            terrain.tileData.clear();
        }

        auto uploadEnd = std::chrono::high_resolution_clock::now();
        terrain.uploadTileDataUs = std::chrono::duration<float, std::micro>(uploadEnd - uploadStart).count();

        // VK-1209: bring up the terrain RVT once bounds are known, then keep its sample resources
        // bound. Only active when config enabled RVT and the pipeline compiled RVT_ENABLED.
        if (vtCache.rvtEnabled && terrain.pipeline->isRVTSampleEnabled())
        {
            rvtWorldMin = terrainGridWorldMin;
            rvtWorldMax = terrainGridWorldMax;
            const bool boundsValid = (rvtWorldMax.x > rvtWorldMin.x) && (rvtWorldMax.y > rvtWorldMin.y);
            if (boundsValid && !terrainRVT)
            {
                // VK-1620: capture the AUTHORED vertical range once, here, alongside the world
                // bounds — every tile in a terrain shares one TerrainTileConfig, so any visible
                // tile answers for all of them. Reading it from tile AABBs instead would make the
                // basis drift with streaming and silently corrupt every already-baked page.
                if (terrain.rvtWorldHeight && !visibleTiles.empty() && visibleTiles.front())
                {
                    const auto& cfg = visibleTiles.front()->config;
                    rvtHeightMin = cfg.minHeight;
                    rvtHeightRange = glm::max(cfg.maxHeight - cfg.minHeight, 0.001f);
                }

                terrainRVT = std::make_unique<TerrainRVTManager>(device);
                TerrainRVTManager::Config cfg;
                cfg.poolBudgetMB = vtCache.rvtPoolBudgetMB;
                cfg.texelsPerMeter = vtCache.rvtTexelsPerMeter;
                cfg.pagesPerFrame = vtCache.pagesPerFrame;
                cfg.evictionAgeFrames = vtCache.evictionAgeFrames;
                cfg.detailMaps = terrain.detailMaps;
                cfg.worldHeight = terrain.rvtWorldHeight;
                terrainRVT->init(cfg, rvtWorldMin, rvtWorldMax);

                if (const auto* pool = terrainRVT->getPool())
                {
                    struct alignas(16) RVTParamsCPU
                    {
                        vt::GPUVTImageInfo img;
                        float worldMinX, worldMinZ;
                        float invExtentX, invExtentZ;
                        float virtualResTexels;
                        // VK-1620: took two of the three spare floats, so the block did not grow.
                        // The scene-mesh blend decodes the world-height plane with these; the
                        // terrain pipeline itself never reads them (it does not sample height).
                        float heightMin, heightRange;
                        float pad2;
                    } params{};
                    static_assert(sizeof(RVTParamsCPU) == 64, "RVTParams must match set-5 UBO (64 bytes)");
                    params.img = terrainRVT->getImageInfo();
                    params.worldMinX = rvtWorldMin.x;
                    params.worldMinZ = rvtWorldMin.y;
                    const glm::vec2 extent = glm::max(rvtWorldMax - rvtWorldMin, glm::vec2(1.0f));
                    params.invExtentX = 1.0f / extent.x;
                    params.invExtentZ = 1.0f / extent.y;
                    params.virtualResTexels = terrainRVT->virtualResTexelsX();
                    params.heightMin = rvtHeightMin;
                    params.heightRange = rvtHeightRange;
                    const vk::ImageView normalView = terrain.detailMaps && pool->planeCount() > 2
                        ? pool->planeView(2) : vk::ImageView{};
                    const vk::ImageView emissionView = terrain.detailMaps && pool->planeCount() > 3
                        ? pool->planeView(3) : vk::ImageView{};
                    terrain.pipeline->updateRVTSampleResources(
                        terrainRVT->getPageTableBuffer(), pool->planeView(0), pool->planeView(1),
                        normalView, emissionView, pool->getSampler(), terrainRVT->getFeedbackBuffer(),
                        &params, sizeof(params));
                }

                // VK-1620: the manager now exists, so the scene mesh pipelines can be compiled with
                // the blend path and pointed at its page table + bindless atlas slots. This is the
                // create edge that matches the two reset edges above.
                applyRVTBlendToggle();
            }
            // Material change -> re-bake all resident fine pages.
            if (terrainRVT && rvtInvalidateAll)
            {
                terrainRVT->invalidateWorldRect(rvtWorldMin, rvtWorldMax);
                rvtInvalidateAll = false;
            }
        }

        terrain.updateUs = std::chrono::duration<float, std::micro>(uploadEnd - frameStart).count();
    }

    void GPUDrivenRenderer::clearTerrainData()
    {
        if (terrain.streamManager)
        {
            terrain.streamManager->clear();
        }
        else if (terrain.adapter)
        {
            terrain.adapter->clear();
        }
        terrain.tileData.clear();
        terrain.currentMaterialPath.clear();
        terrain.layerData.clear();

        if (terrain.pipeline)
        {
            terrain.pipeline->updateTileData({});
            terrain.pipeline->updateTerrainLayerInfo({});
        }

        if (terrain.meshBuffer)
        {
            terrain.meshBuffer->clear();
        }

        clearVegetationData();
    }

    void GPUDrivenRenderer::evictTerrainTile(int32_t coordX, int32_t coordZ)
    {
        if (terrain.streamManager)
        {
            terrain.streamManager->evictTile(coordX, coordZ);
        }
        else if (terrain.adapter)
        {
            render::gpudriven::TerrainTileKey key{coordX, coordZ};
            for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVEL_COUNT; ++lod)
            {
                terrain.adapter->removeTileLOD(key, lod);
            }
        }
        if (terrain.adapter)
            terrain.adapter->markGPUTileDataDirty();
    }

    void GPUDrivenRenderer::setSelectedTerrainTile(int32_t coordX, int32_t coordZ)
    {
        if (terrain.adapter)
            terrain.adapter->setSelectedTile(coordX, coordZ);
    }

    void GPUDrivenRenderer::clearSelectedTerrainTile()
    {
        if (terrain.adapter)
            terrain.adapter->clearSelectedTile();
    }

    void GPUDrivenRenderer::renderTerrainDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                              uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!initialized || !terrain.renderingEnabled || !terrain.pipeline || !meshShaderPipeline)
        {
            return;
        }

        if (terrain.tileData.empty())
        {
            return;
        }

        // VK-1415: terrain honors the per-camera render-layer mask (main view = all layers).
        if (((1u << (terrain.renderLayer & 31u)) & getThreadLocalRTTCullingMask()) == 0u)
            return;

        terrain.pipeline->updateSharedDescriptors(
            iblDescriptorSet,
            bindlessTextures->getDescriptorSet(),
            lightBufferManager->getDescriptorSet(),
            clusterGridManager->getDescriptorSet(),
            lightCullingPipeline->getDescriptorSet(),
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowDataDescSet() : vk::DescriptorSet{},
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowTextureDescSet() : vk::DescriptorSet{}
        );

        float dispatchWidth, dispatchHeight;
        if (screenWidth > 0 && screenHeight > 0)
        {
            dispatchWidth = static_cast<float>(screenWidth);
            dispatchHeight = static_cast<float>(screenHeight);
        }
        else
        {
            auto extent = swapChain.getSwapchainExtent();
            dispatchWidth = static_cast<float>(extent.width);
            dispatchHeight = static_cast<float>(extent.height);
        }

        float terrainDistSq = 0.0f;
        if (culling.distanceCullingEnabled)
        {
            float d = culling.categoryDistances[ObjectCategory::Terrain];
            terrainDistSq = d * d;
        }
        terrain.pipeline->setTerrainMaxDrawDistSq(terrainDistSq);
        terrain.pipeline->setMeshletOcclusionCullingEnabled(culling.meshletOcclusionCullingEnabled);
        terrain.pipeline->setHiZMipLevels(prepassHiZMipLevels);

        uint32_t viewMode = culling.currentViewMode;
        if (culling.meshletFrustumCullingEnabled) viewMode |= TERRAIN_CULL_FRUSTUM_BIT;
        if (culling.meshletBackfaceCullingEnabled) viewMode |= TERRAIN_CULL_BACKFACE_BIT;

        terrain.pipeline->dispatch(
            cmd,
            currentImageIndex,
            viewMode,
            dispatchWidth,
            dispatchHeight,
            terrain.lodBias,
            terrain.errorThreshold,
            terrain.textureScale
        );
    }

    void GPUDrivenRenderer::setBrushOverlay(const glm::vec3& worldPos, float worldRadius, float falloff, float shape, float stampRotation)
    {
        if (terrain.pipeline)
        {
            terrain.pipeline->setBrushOverlay(worldPos, worldRadius, falloff, shape);
            terrain.pipeline->setStampRotation(stampRotation);
        }
    }

    void GPUDrivenRenderer::setStampOverlay(vk::Buffer buffer, uint32_t width, uint32_t height, float rotation)
    {
        if (terrain.pipeline)
        {
            terrain.pipeline->setStampOverlay(buffer, width, height, rotation);
        }
    }

    void GPUDrivenRenderer::clearStampOverlay()
    {
        if (terrain.pipeline)
        {
            terrain.pipeline->clearStampOverlay();
        }
    }

    void GPUDrivenRenderer::setTileDataLoader(TerrainStreamManager::TileDataLoader loader)
    {
        if (terrain.streamManager)
        {
            terrain.streamManager->setTileDataLoader(std::move(loader));
        }
        else
        {
            terrain.pendingTileDataLoader = std::move(loader);
        }
    }

    void GPUDrivenRenderer::setTileRAMEvictor(TerrainStreamManager::TileRAMEvictor evictor)
    {
        if (terrain.streamManager)
        {
            terrain.streamManager->setTileRAMEvictor(std::move(evictor));
        }
        else
        {
            terrain.pendingTileRAMEvictor = std::move(evictor);
        }
    }

    void GPUDrivenRenderer::setTileLoadContextProvider(TerrainStreamManager::TileLoadContextProvider loader)
    {
        if (terrain.streamManager)
        {
            terrain.streamManager->setTileLoadContextProvider(std::move(loader));
        }
        else
        {
            terrain.pendingTileLoadContextProvider = std::move(loader);
        }
    }

    const TerrainStreamingStats* GPUDrivenRenderer::getTerrainStreamingStats() const
    {
        if (terrain.streamManager)
        {
            return &terrain.streamManager->getStats();
        }
        return nullptr;
    }

    TerrainCullingStats GPUDrivenRenderer::getTerrainCullingStats()
    {
        if (terrain.pipeline)
        {
            return terrain.pipeline->readStats();
        }
        return {};
    }
}
