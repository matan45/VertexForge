#include "GPUDrivenRenderer.hpp"
#include "SelectionMaskPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../water/OceanFFTResources.hpp"
#include "../../../services/data/OceanData.hpp"
#include "../../../utilities/water/WaterTileGrid.hpp"
#include "../../../utilities/water/HexTiling.hpp"
#include "../../../utilities/water/DisplacementSampling.hpp"
#include "../../../utilities/water/ShoreDepthField.hpp"
#include "../../../utilities/water/ShoalingMath.hpp"
#include "../../../utilities/water/ShoreWaveMath.hpp"
#include "../../../utilities/water/WaterBodyMath.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <chrono>

namespace render::gpudriven
{
    void GPUDrivenRenderer::initWaterSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                  const std::vector<vk::Format>& colorFormats, vk::Format depthFormat,
                                                  vk::ImageView sceneDepthView)
    {
        water.meshBuffer = std::make_unique<render::water::WaterMeshBuffer>();
        water.meshBuffer->init(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool()
        );

        // VK-1605: the shore-depth texture must exist BEFORE the refraction resources, which bind
        // it at set 9 binding 3. Its size is a compile-time constant, so it is created once and
        // never recreated - only its contents are re-uploaded.
        water.shoreDepthResources = std::make_unique<render::water::WaterShoreDepthResources>(device);
        water.shoreDepthResources->init();

        // VK-1606: same rule for the ripple patch at set 9 binding 4 - created once, before the
        // refraction resources, and never recreated.
        water.rippleSim = std::make_unique<render::water::WaterRippleSim>(device);
        water.rippleSim->init();

        // Create refraction resources before pipeline so we have the descriptor set layout
        water.refractionResources = std::make_unique<render::water::WaterRefractionResources>(device);
        water.refractionResources->init(
            swapChain.getSceneColorFormat(),
            swapChain.getSwapchainExtent().width,
            swapChain.getSwapchainExtent().height,
            sceneDepthView,
            water.shoreDepthResources->getImageView(),
            water.shoreDepthResources->getSampler(),
            water.rippleSim->getOutputView(),
            water.rippleSim->getSampler());

        // Ocean texture layout (from multi-band descriptor if initialized, otherwise WaterPipeline creates dummy)
        vk::DescriptorSetLayout oceanLayout{};
        if (water.multiBandOceanLayout)
            oceanLayout = water.multiBandOceanLayout;

        vk::DescriptorSetLayout refractionLayout = water.refractionResources->getDescriptorSetLayout();

        water.pipeline = std::make_unique<render::water::WaterPipeline>(device, swapChain);
        water.pipeline->init({
            iblDescriptorSetLayout,
            lightBufferManager->getDescriptorSetLayout(),
            clusterGridManager->getDescriptorSetLayout(),
            lightCullingPipeline->getDescriptorSetLayout(),
            shadowSystem->getShadowDataLayout(),
            shadowSystem->getShadowTextureLayout(),
            oceanLayout,
            refractionLayout,
            colorFormats, depthFormat
        });

        // VK-1605: RTT / reflection-probe views bind the pipeline's DUMMY set 9 (they must not
        // sample the main view's scene colour and depth), but they still have to displace their
        // water identically or they reflect a surface that is not there. The shore field is
        // view-independent, so the dummy set gets the real texture.
        water.pipeline->updateDummyShoreDepth(water.shoreDepthResources->getImageView(),
                                              water.shoreDepthResources->getSampler());
        water.pipeline->updateDummyRipple(water.rippleSim->getOutputView(),
                                          water.rippleSim->getSampler());
    }

    void GPUDrivenRenderer::updateWater(const services::OceanVisualSettings& visualSettings,
                                          float baseWaterHeight,
                                          const glm::vec3& cameraPosition,
                                          float oceanPatchSize,
                                          bool worldMode,
                                          const ::water::WaterTileGrid* tileGrid,
                                          const ::water::ShoreDepthField* shoreField,
                                          const std::vector<::water::WaterBodyDesc>& waterBodies,
                                          bool oceanActive)
    {
        auto updateStart = std::chrono::high_resolution_clock::now();
        if (!initialized || !water.renderingEnabled || !water.pipeline)
            return;

        float tileSize = oceanPatchSize * 2.0f;

        // VK-1607: publish the tile layout the CPU height sampler needs to reproduce this frame's
        // per-tile band LOD. Set before the branches below so every path leaves it consistent.
        const bool worldTiles = worldMode && tileGrid && tileGrid->tileCount() > 0;
        water.lodWorldMode = oceanActive && worldTiles;
        water.lodTileSize = tileSize > 0.0f ? tileSize : 1.0f;
        water.lodCameraXZ = glm::vec2(cameraPosition.x, cameraPosition.z);
        water.lodGridOriginXZ = glm::vec2(std::floor(cameraPosition.x / water.lodTileSize) * water.lodTileSize,
                                          std::floor(cameraPosition.z / water.lodTileSize) * water.lodTileSize);

        if (!oceanActive)
        {
            // VK-1607: no ocean, so no ocean tiles - the water bodies appended below are the whole
            // draw. Everything downstream (the UBO fill, the push constants, the descriptor update)
            // still runs, because the bodies need it all.
            water.tileData.clear();
            for (uint32_t lod = 0; lod < render::water::WATER_LOD_COUNT; ++lod)
                water.lodTileCounts[lod] = 0;
        }
        else if (worldTiles)
        {
            // World mode: sector-driven tiles from WaterTileGrid
            water.tileData.clear();
            uint32_t gridLodCounts[render::water::WATER_LOD_COUNT] = {};
            tileGrid->buildGPUTileData(cameraPosition, tileSize, baseWaterHeight,
                                        water.tileData, gridLodCounts);
            for (uint32_t lod = 0; lod < render::water::WATER_LOD_COUNT; ++lod)
                water.lodTileCounts[lod] = gridLodCounts[lod];
        }
        else
        {
            // Editor mode: generate 9x9 grid centered on camera
            constexpr int GRID_HALF = 4;

            float snappedX = std::floor(cameraPosition.x / tileSize) * tileSize;
            float snappedZ = std::floor(cameraPosition.z / tileSize) * tileSize;

            std::array<std::vector<render::water::WaterTileGPUData>, render::water::WATER_LOD_COUNT> lodBuckets;
            for (auto& bucket : lodBuckets) bucket.reserve(32);

            for (int tz = -GRID_HALF; tz <= GRID_HALF; ++tz)
            {
                for (int tx = -GRID_HALF; tx <= GRID_HALF; ++tx)
                {
                    float tileOriginX = snappedX + tx * tileSize;
                    float tileOriginZ = snappedZ + tz * tileSize;

                    int ring = std::max(std::abs(tx), std::abs(tz));

                    uint32_t lod;
                    if (ring <= 1) lod = 0;
                    else if (ring <= 2) lod = 1;
                    else if (ring <= 3) lod = 2;
                    else lod = 3;

                    // VK-1607: .y is the size along Z (square here) and .w carries the per-tile
                    // flags - every band enabled, not a water body.
                    render::water::WaterTileGPUData tile;
                    tile.worldOriginAndSize = glm::vec4(tileOriginX, tileSize, tileOriginZ, tileSize);
                    tile.heightAndWave = glm::vec4(baseWaterHeight, 1.0f, static_cast<float>(lod),
                                                   static_cast<float>(::water::WATER_TILE_OCEAN_FLAGS));
                    lodBuckets[lod].push_back(tile);
                }
            }

            water.tileData.clear();
            for (uint32_t lod = 0; lod < render::water::WATER_LOD_COUNT; ++lod)
            {
                water.lodTileCounts[lod] = static_cast<uint32_t>(lodBuckets[lod].size());
                water.tileData.insert(water.tileData.end(), lodBuckets[lod].begin(), lodBuckets[lod].end());
            }
        }

        // VK-1607: water bodies contribute one tile each, PREPENDED into the LOD 0 range. Tiles are
        // flattened LOD 0 first, so the head of the array is the head of LOD 0 - which is exactly the
        // part clampLodTileCounts never trims. A lake must not disappear because the ocean filled the
        // 128-instance budget.
        if (!waterBodies.empty())
        {
            std::vector<render::water::WaterTileGPUData> bodyTiles;
            bodyTiles.reserve(waterBodies.size());
            for (const auto& body : waterBodies)
                bodyTiles.push_back(::water::makeBodyTile(body));

            water.tileData.insert(water.tileData.begin(), bodyTiles.begin(), bodyTiles.end());
            water.lodTileCounts[0] += static_cast<uint32_t>(bodyTiles.size());
        }

        // The instance budget is now shared between the ocean and the bodies. updateTileData clamps
        // only the memcpy, so without trimming lodTileCounts too, renderMultiLOD would issue draws
        // whose firstInstance runs past the end of the SSBO.
        {
            const uint32_t kept = ::water::clampLodTileCounts(water.lodTileCounts,
                                                              ::water::MAX_WATER_GPU_INSTANCES);
            if (kept < water.tileData.size())
            {
                if (!water.tileBudgetWarned)
                {
                    water.tileBudgetWarned = true;
                    vfLogWarning("Water tile budget exceeded: {} tiles requested, {} drawn (limit {})",
                                 water.tileData.size(), kept, ::water::MAX_WATER_GPU_INSTANCES);
                }
                water.tileData.resize(kept);
            }
        }

        water.meshBuffer->updateTileData(water.tileData);

        water.pipeline->updateDescriptors(
            water.meshBuffer->getTileSSBO(),
            static_cast<uint32_t>(water.tileData.size())
        );

        water.cachedPushConstants.shallowColor = visualSettings.shallowColor;
        water.cachedPushConstants.deepColor = visualSettings.deepColor;
        water.cachedPushConstants.maxVisibleDepth = visualSettings.maxVisibleDepth;
        water.cachedPushConstants.fresnelPower = visualSettings.fresnelPower;
        water.cachedPushConstants.refractionStrength = visualSettings.refractionStrength;
        water.cachedPushConstants.refractionChromatic = visualSettings.refractionChromatic;
        water.cachedPushConstants.refractionDepthScale = visualSettings.refractionDepthScale;
        water.cachedPushConstants.shoreFoamRange = visualSettings.shoreFoamRange;
        water.cachedPushConstants.shoreFoamIntensity = visualSettings.shoreFoamIntensity;
        water.cachedPushConstants.shoreBreakingStrength = visualSettings.shoreBreakingStrength;

        // Update caustic params UBO
        if (water.causticsResources && water.causticsResources->isInitialized())
        {
            render::water::CausticParams params;
            params.waterHeight = baseWaterHeight;
            params.causticStrength = visualSettings.causticStrength;
            params.depthFalloff = visualSettings.causticDepthFalloff;
            params.patchSize = oceanPatchSize;
            params.shoreWetRange = visualSettings.shoreWetRange;
            params.shoreWetDarkening = visualSettings.shoreWetDarkening;
            params.shoreWetRoughness = visualSettings.shoreWetRoughness;
            water.causticsResources->updateParams(params);
        }

        // VK-1605: adopt a completed shore-depth bake. The CPU copy serves BOTH the GPU upload and
        // getOceanHeightAt, so buoyancy and pixels can never disagree about where the bottom is.
        // Copying only on a version change keeps this to one 256 KB memcpy every few seconds.
        if (shoreField)
        {
            water.shoreFieldOrigin = shoreField->origin();
            water.shoreFieldWindow = shoreField->windowSize();
            water.shoreFieldResolution = shoreField->resolution();

            if (shoreField->version() != water.shoreFieldVersion)
            {
                water.shoreFieldVersion = shoreField->version();
                water.shoreDepthData = shoreField->data();
                if (water.shoreDepthResources && water.shoreDepthResources->isInitialized())
                    water.shoreDepthResources->stage(water.shoreDepthData);
            }
        }

        // Per-band characteristic wavelength: the Pierson-Moskowitz peak for that band's own wind
        // speed, which is what actually decides the depth at which the band feels the bottom.
        // patchSize is only a tiling period and would put swell's onset in 250 m of water.
        glm::vec3 bandWavelength{0.0f};
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (!water.oceanBands[i] || !water.oceanBands[i]->isInitialized())
                continue;
            const auto& cfg = water.oceanBands[i]->getConfig();
            bandWavelength[static_cast<int>(i)] =
                ::water::characteristicWavelength(cfg.windSpeed) * visualSettings.shoalingWavelengthScale;
        }

        // VK-1606: re-centre the ripple patch on the camera. Snapped to the texel lattice, otherwise
        // the field is resampled at a sub-texel offset every frame and the whole surface crawls.
        // The origin is computed HERE (not inside the sim) so the UBO written below and the compute
        // push constants recorded later in the frame cannot disagree about the window.
        {
            water.ripplePatchSize = visualSettings.ripplePatchSize > 0.0f
                ? visualSettings.ripplePatchSize
                : ::water::RIPPLE_DEFAULT_PATCH_SIZE;
            water.rippleOrigin = ::water::rippleSnapOrigin(
                glm::vec2(cameraPosition.x, cameraPosition.z), water.ripplePatchSize);
            water.rippleEnabled = visualSettings.rippleSimEnabled;

            if (water.rippleSim && water.rippleSim->isInitialized())
            {
                render::water::RippleSimParams rippleParams;
                rippleParams.origin = water.rippleOrigin;
                rippleParams.patchSize = water.ripplePatchSize;
                rippleParams.waveSpeed = visualSettings.rippleWaveSpeed;
                rippleParams.damping = visualSettings.rippleDamping;
                rippleParams.foamGain = visualSettings.rippleFoamGain;
                rippleParams.foamDecay = visualSettings.rippleFoamDecay;
                rippleParams.enabled = water.rippleEnabled;
                water.rippleSim->setParams(rippleParams);
            }
        }

        // VK-1604: extended visual params (set 9 binding 2). Filled once per frame; per-view
        // gating rides on WaterPushConstants::viewFlagMask instead (see renderWaterDraw).
        {
            render::water::WaterExtendedParams ext{};
            ext.absorptionCoeff = glm::vec4(visualSettings.absorptionCoeff, 0.0f);
            ext.scatterColor = glm::vec4(visualSettings.scatteringColor, 0.0f);
            ext.scatterCoeff = glm::vec4(glm::vec3(visualSettings.scatterCoeff), 0.0f);

            ext.ssrIntensity = visualSettings.ssrIntensity;
            ext.ssrMaxDistance = visualSettings.ssrMaxDistance;
            ext.ssrThickness = visualSettings.ssrThickness;
            ext.ssrMaxSteps = visualSettings.ssrMaxSteps;
            ext.absorptionMaxDistance = visualSettings.absorptionMaxDistance;

            ext.hexBlendExponent = visualSettings.hexBlendContrast;
            ext.hexCellScale0 = visualSettings.hexCellScale;
            ext.hexCellScale1 = visualSettings.hexCellScale;
            ext.hexCellScale2 = visualSettings.hexCellScale;
            ext.hexPerBandMask = visualSettings.hexBandMask;

            // VK-1605 shoreline rows
            ext.shoalingStrength = visualSettings.shoalingStrength;
            ext.shoalingGamma = visualSettings.shoalingGamma;
            ext.shoreEdgeFadeStart = visualSettings.shoreEdgeFadeStart;

            const float invWindow = water.shoreFieldWindow > 0.0f ? 1.0f / water.shoreFieldWindow : 1.0f;
            ext.shoreFieldOrigin = glm::vec4(water.shoreFieldOrigin.x, water.shoreFieldOrigin.y,
                                             water.shoreFieldWindow, invWindow);
            ext.bandWavelength = glm::vec4(bandWavelength, visualSettings.shoalingMinDepth);
            ext.shoreWaveA = glm::vec4(visualSettings.shoreWaveAmplitude,
                                       visualSettings.shoreWaveLength,
                                       visualSettings.shoreWaveSpeed,
                                       visualSettings.shoreWaveBreakDepth);
            ext.shoreWaveB = glm::vec4(visualSettings.shoreWaveBreakRange,
                                       visualSettings.shoreWaveCrestFoam,
                                       visualSettings.shoreWaveCrestFoamThreshold,
                                       visualSettings.shoreWaveLean);

            // VK-1606 ripple rows
            const float invPatch = water.ripplePatchSize > 0.0f ? 1.0f / water.ripplePatchSize : 1.0f;
            ext.ripplePatch = glm::vec4(water.rippleOrigin.x, water.rippleOrigin.y,
                                        water.ripplePatchSize, invPatch);
            ext.rippleParams = glm::vec4(visualSettings.rippleHeightScale,
                                         visualSettings.rippleNormalScale,
                                         visualSettings.rippleFoamScale,
                                         visualSettings.rippleEdgeFadeStart);

            uint32_t flags = 0;
            if (visualSettings.ssrEnabled) flags |= render::water::WATER_FLAG_SSR;
            if (visualSettings.beerLambertEnabled) flags |= render::water::WATER_FLAG_ABSORPTION;
            if (visualSettings.hexTilingEnabled) flags |= render::water::WATER_FLAG_HEX;
            if (visualSettings.ssrDebugView) flags |= render::water::WATER_FLAG_SSR_DEBUG;
            // The shoreline features need a field that has actually finished a bake; before that
            // the texture reads SHORE_FIELD_DEEP everywhere and would simply do nothing, but
            // leaving the bits clear also keeps the fragment shore-foam path byte-identical.
            const bool shoreFieldLive = water.shoreFieldVersion > 0;
            if (shoreFieldLive) flags |= render::water::WATER_FLAG_SHORE_FIELD;
            if (shoreFieldLive && visualSettings.shoalingEnabled)
                flags |= render::water::WATER_FLAG_SHOALING;
            if (shoreFieldLive && visualSettings.shoreWavesEnabled &&
                visualSettings.shoreWaveAmplitude > 0.0f)
                flags |= render::water::WATER_FLAG_SHORE_WAVES;
            // VK-1606: only once the sim has actually stepped. Until then the output image is the
            // zero-fill from init, which would displace nothing but would still cost the taps.
            if (water.rippleEnabled && water.rippleSim && water.rippleSim->hasSimulated())
                flags |= render::water::WATER_FLAG_RIPPLES;

            // VK-1607: the footprints ocean fragments must be discarded inside. Only the nearest
            // MAX_WATER_BODY_CLIP_RECTS fit; the rest still draw their own surface, they just stop
            // suppressing the ocean underneath. Sorting by distance means the ones actually filling
            // the screen are the ones that get the slots.
            if (!waterBodies.empty())
            {
                std::vector<uint32_t> order(waterBodies.size());
                for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;

                const glm::vec2 camXZ(cameraPosition.x, cameraPosition.z);
                std::sort(order.begin(), order.end(),
                          [&](uint32_t a, uint32_t b)
                          {
                              const glm::vec2 va = waterBodies[a].center - camXZ;
                              const glm::vec2 vb = waterBodies[b].center - camXZ;
                              const float da = glm::dot(va, va);
                              const float db = glm::dot(vb, vb);
                              if (da != db) return da < db;
                              return a < b;   // stable, so the set does not flicker between frames
                          });

                const uint32_t clipCount = std::min(static_cast<uint32_t>(order.size()),
                                                    ::water::MAX_WATER_BODY_CLIP_RECTS);
                for (uint32_t i = 0; i < clipCount; ++i)
                    ext.bodyClipRects[i] = ::water::clipRect(waterBodies[order[i]]);
                ext.bodyClipCount = clipCount;

                if (clipCount > 0)
                    flags |= render::water::WATER_FLAG_BODY_CLIP;
            }

            ext.flags = flags;

            water.cachedExtendedParams = ext;
            if (water.refractionResources && water.refractionResources->isInitialized())
                water.refractionResources->updateParams(ext);

            // VK-1607: the DUMMY set 9 gets this frame's params too. It used to be written once at
            // init with the struct defaults, so every view that binds it — RTT / reflection probes,
            // and the ocean-disabled path — ran with flags = 0: no hex, no shoaling, no shore waves,
            // no ripples and, since this story, no water-body clip either, which is what made a
            // probe show the sea through a raised pool's floor.
            //
            // Masked by WATER_VIEW_FLAGS_RTT rather than copied verbatim. The RTT path already
            // clears SSR and absorption through pc.viewFlagMask, but the ocean-disabled path does
            // not — and both of them are looking at a 1x1 stand-in instead of the scene colour copy
            // and depth image those two features read. Masking here is what makes the buffer correct
            // for every consumer of the dummy set rather than just for RTT.
            if (water.pipeline)
            {
                render::water::WaterExtendedParams dummyExt = ext;
                dummyExt.flags = ext.flags & render::water::WATER_VIEW_FLAGS_RTT;
                water.pipeline->updateDummyParams(dummyExt);
            }
        }

        // VK-1604: CPU buoyancy mirrors the shader's hex blend, so the height sampler needs the
        // same parameters. Written here on the render thread and read by the physics worker
        // through the injected sampler — same benign publish pattern as cachedPushConstants.
        water.hexTilingEnabled = visualSettings.hexTilingEnabled;
        water.hexBandMask = visualSettings.hexBandMask;
        water.hexCellScale = visualSettings.hexCellScale;
        water.hexBlendContrast = visualSettings.hexBlendContrast;

        // VK-1605: the same publish for the shoreline params. getOceanHeightAt reads these to apply
        // the identical per-band shoaling and shore-wave surge the vertex shader just applied.
        water.shoalingEnabled = water.shoreFieldVersion > 0 && visualSettings.shoalingEnabled;
        water.shoalingStrength = visualSettings.shoalingStrength;
        water.shoalingGamma = visualSettings.shoalingGamma;
        water.shoalingMinDepth = visualSettings.shoalingMinDepth;
        water.shoreEdgeFadeStart = visualSettings.shoreEdgeFadeStart;
        water.bandWavelength = bandWavelength;

        water.shoreWavesEnabled = water.shoreFieldVersion > 0 && visualSettings.shoreWavesEnabled &&
                                  visualSettings.shoreWaveAmplitude > 0.0f;
        water.shoreWaveParams.amplitude = visualSettings.shoreWaveAmplitude;
        water.shoreWaveParams.length = visualSettings.shoreWaveLength;
        water.shoreWaveParams.speed = visualSettings.shoreWaveSpeed;
        water.shoreWaveParams.breakDepth = visualSettings.shoreWaveBreakDepth;
        water.shoreWaveParams.breakRange = visualSettings.shoreWaveBreakRange;
        water.shoreWaveParams.crestFoam = visualSettings.shoreWaveCrestFoam;
        water.shoreWaveParams.crestFoamThreshold = visualSettings.shoreWaveCrestFoamThreshold;

        auto updateEnd = std::chrono::high_resolution_clock::now();
        water.updateUs = std::chrono::duration<float, std::micro>(updateEnd - updateStart).count();
    }

    void GPUDrivenRenderer::renderWaterDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        auto renderStart = std::chrono::high_resolution_clock::now();
        if (!initialized || !water.renderingEnabled || !water.pipeline || water.tileData.empty())
            return;

        // VK-1415: water honors the per-camera render-layer mask (main view = all layers).
        if (((1u << (water.renderLayer & 31u)) & getThreadLocalRTTCullingMask()) == 0u)
            return;

        // Set per-band ocean push constant fields
        uint32_t bandMask = 0;
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (water.oceanBands[i] && water.oceanBands[i]->isInitialized())
                bandMask |= (1u << i);
        }
        water.cachedPushConstants.bandEnableMask = bandMask;

        if (water.oceanEnabled && water.oceanBands[0] && water.oceanBands[0]->isInitialized())
        {
            const auto& cfg0 = water.oceanBands[0]->getConfig();
            water.cachedPushConstants.oceanChoppiness = cfg0.choppiness;
            water.cachedPushConstants.oceanPatchSize0 = cfg0.patchSize;
            water.cachedPushConstants.oceanFoamThreshold = cfg0.foamThreshold;
        }
        else
        {
            water.cachedPushConstants.oceanChoppiness = 0.0f;
            water.cachedPushConstants.oceanPatchSize0 = 1.0f;
            water.cachedPushConstants.oceanFoamThreshold = 0.0f;
        }

        water.cachedPushConstants.oceanPatchSize1 = (water.oceanBands[1] && water.oceanBands[1]->isInitialized())
            ? water.oceanBands[1]->getConfig().patchSize : 1.0f;
        water.cachedPushConstants.oceanPatchSize2 = (water.oceanBands[2] && water.oceanBands[2]->isInitialized())
            ? water.oceanBands[2]->getConfig().patchSize : 1.0f;

        // VK-1604: set 9 (scene color copy + scene depth) belongs to the MAIN view. An RTT /
        // reflection-probe view renders into its own depth image and never refreshes the color
        // copy, so binding set 9 there would sample the main view's resources - and its depth
        // image is in DepthStencilAttachmentOptimal on that submission, which makes the
        // DepthStencilReadOnlyOptimal descriptor a validation error, not merely wrong pixels.
        // Bind the dummy set instead (renderMultiLOD substitutes it for a null handle) and
        // suppress refraction so the dummy texture never shows. The override lives on a local
        // copy - mutating water.cachedPushConstants would leak into the main-view draw.
        //
        // VK-1607: the refraction suppression follows whether the DUMMY set will be bound, not
        // whether this is an RTT view. The dummy is also what gets bound when the refraction
        // resources are absent, and its binding 0 is a 1x1 stand-in - sampling that as refraction
        // colour tints the whole surface with a single texel.
        const bool inRTT = isThreadLocalRTTContext();
        const bool refractionReady = water.refractionResources && water.refractionResources->isInitialized();
        const bool usingRefractionDummy = inRTT || !refractionReady;

        render::water::WaterPushConstants drawPushConstants = water.cachedPushConstants;
        drawPushConstants.viewFlagMask = inRTT ? render::water::WATER_VIEW_FLAGS_RTT
                                               : render::water::WATER_VIEW_FLAGS_ALL;
        if (usingRefractionDummy)
            drawPushConstants.refractionStrength = 0.0f;

        render::water::WaterRenderDescriptors waterDescriptors{
            iblDescriptorSet,
            lightBufferManager->getDescriptorSet(),
            clusterGridManager->getDescriptorSet(),
            lightCullingPipeline->getDescriptorSet(),
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowDataDescSet() : vk::DescriptorSet{},
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowTextureDescSet() : vk::DescriptorSet{},
            (water.oceanEnabled && water.multiBandDescriptorValid && water.multiBandOceanDescSet)
                ? water.multiBandOceanDescSet : vk::DescriptorSet{},
            usingRefractionDummy ? vk::DescriptorSet{} : water.refractionResources->getDescriptorSet()
        };
        water.pipeline->renderMultiLOD(cmd, waterDescriptors, *water.meshBuffer,
                                       drawPushConstants, water.lodTileCounts);

        auto renderEnd = std::chrono::high_resolution_clock::now();
        water.renderUs = std::chrono::duration<float, std::micro>(renderEnd - renderStart).count();
    }

    void GPUDrivenRenderer::clearWaterData()
    {
        water.tileData.clear();
    }

    void GPUDrivenRenderer::initOceanFFT(const std::array<render::water::OceanFFTConfig, 3>& bandConfigs,
                                          const std::array<bool, 3>& bandEnabled)
    {
        device.getLogicalDevice().waitIdle();

        water.activeBandCount = 0;
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (bandEnabled[i])
            {
                if (!water.oceanBands[i])
                    water.oceanBands[i] = std::make_unique<render::water::OceanFFT>(device);
                water.oceanBands[i]->init(bandConfigs[i]);
                water.activeBandCount++;
            }
            else
            {
                if (water.oceanBands[i])
                {
                    water.oceanBands[i]->cleanup();
                    water.oceanBands[i].reset();
                }
            }
        }
        water.oceanEnabled = water.activeBandCount > 0;

        // Create composite 6-binding descriptor set
        createMultiBandOceanDescriptor();

        // Create caustics from band 0 (swell) - only if caustic view is available
        if (water.oceanBands[0] && water.oceanBands[0]->isInitialized()
            && water.oceanBands[0]->getCausticView())
        {
            water.causticsResources = std::make_unique<render::water::WaterCausticsResources>(device);
            water.causticsResources->init(water.oceanBands[0]->getCausticView());
        }

        // Recreate water pipeline with multi-band ocean texture layout
        if (water.pipeline)
        {
            vk::DescriptorSetLayout refractionLayout{};
            if (water.refractionResources && water.refractionResources->isInitialized())
                refractionLayout = water.refractionResources->getDescriptorSetLayout();

            water.pipeline->recreate({
                cachedIBLLayout,
                lightBufferManager->getDescriptorSetLayout(),
                clusterGridManager->getDescriptorSetLayout(),
                lightCullingPipeline->getDescriptorSetLayout(),
                shadowSystem->getShadowDataLayout(),
                shadowSystem->getShadowTextureLayout(),
                water.multiBandOceanLayout,
                refractionLayout,
                cachedColorFormats,
                cachedDepthFormat
            });
        }

        // Recreate mesh shader pipelines with caustic layout
        if (water.causticsResources && water.causticsResources->isInitialized())
        {
            vk::DescriptorSetLayout causticLayout = water.causticsResources->getDescriptorSetLayout();
            vk::DescriptorSet causticDescSet = water.causticsResources->getDescriptorSet();

            if (meshShaderPipeline && shadowSystem)
            {
                vk::DescriptorSetLayout giLayout{};
                if (giCascadeManager)
                {
                    auto* storage = giCascadeManager->getProbeStorage();
                    if (storage && storage->isInitialized())
                        giLayout = storage->getSamplingLayout();
                }

                MeshPipelineInitInfo pipelineInfo{
                    .iblLayout = cachedIBLLayout,
                    .bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout(),
                    .boneMatrixLayout = boneMatrixManager->getDescriptorSetLayout(),
                    .lightDataLayout = lightBufferManager->getDescriptorSetLayout(),
                    .clusterGridLayout = clusterGridManager->getDescriptorSetLayout(),
                    .cullingOutputLayout = lightCullingPipeline->getDescriptorSetLayout(),
                    .shadowDataLayout = shadowSystem->getShadowDataLayout(),
                    .shadowTextureLayout = shadowSystem->getShadowTextureLayout(),
                    .giProbeDataLayout = giLayout,
                    .causticLayout = causticLayout,
                    .worldMaskLayout = currentWorldMaskLayout(),
                    .selectionCoverageLayout = selectionMaskPipeline ? selectionMaskPipeline->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
                    .colorAttachmentFormats = cachedColorFormats,
                    .depthAttachmentFormat = cachedDepthFormat
                };

                meshShaderPipeline->recreate(pipelineInfo);
                meshShaderPipeline->updateCausticDescriptor(causticDescSet);
                if (giLayout && giCascadeManager)
                {
                    auto* storage = giCascadeManager->getProbeStorage();
                    meshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                }

                if (transparentMeshShaderPipeline)
                {
                    pipelineInfo.transparentMode = true;
                    transparentMeshShaderPipeline->recreate(pipelineInfo);
                    transparentMeshShaderPipeline->updateCausticDescriptor(causticDescSet);
                    if (giLayout && giCascadeManager)
                    {
                        auto* storage = giCascadeManager->getProbeStorage();
                        transparentMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                    }
                    pipelineInfo.transparentMode = false;
                }

                if (wboitMeshShaderPipeline && !cachedWBOITColorFormats.empty())
                {
                    pipelineInfo.colorAttachmentFormats = cachedWBOITColorFormats;
                    pipelineInfo.depthAttachmentFormat = cachedWBOITDepthFormat;
                    pipelineInfo.wboitMode = true;
                    wboitMeshShaderPipeline->recreate(pipelineInfo);
                    wboitMeshShaderPipeline->updateCausticDescriptor(causticDescSet);
                    if (giLayout && giCascadeManager)
                    {
                        auto* storage = giCascadeManager->getProbeStorage();
                        wboitMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                    }
                }
            }

            // Recreate terrain pipeline with caustic layout
            if (terrain.pipeline)
            {
                terrain.pipeline->setCausticEnabled(true, causticLayout);
                terrain.pipeline->recreate(cachedIBLLayout,
                                           bindlessTextures->getDescriptorSetLayout(),
                                           meshShaderPipeline->getMeshletDataLayout(),
                                           meshShaderPipeline->getVertexDataLayout(),
                                           lightBufferManager->getDescriptorSetLayout(),
                                           clusterGridManager->getDescriptorSetLayout(),
                                           lightCullingPipeline->getDescriptorSetLayout(),
                                           shadowSystem->getShadowDataLayout(),
                                           shadowSystem->getShadowTextureLayout(),
                                           cachedColorFormats, cachedDepthFormat);
                terrain.pipeline->updateCausticDescriptor(causticDescSet);
            }
        }

        vfLogInfo("GPUDrivenRenderer: Ocean FFT initialized with {} active bands", water.activeBandCount);
    }

    void GPUDrivenRenderer::createMultiBandOceanDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Cleanup old
        if (water.multiBandOceanPool)
        {
            vkDevice.destroyDescriptorPool(water.multiBandOceanPool);
            water.multiBandOceanPool = nullptr;
        }
        if (water.multiBandOceanLayout)
        {
            vkDevice.destroyDescriptorSetLayout(water.multiBandOceanLayout);
            water.multiBandOceanLayout = nullptr;
        }
        water.multiBandOceanDescSet = nullptr;
        water.multiBandDescriptorValid = false;

        // Create layout: 6 combined image samplers (vertex + fragment)
        std::array<vk::DescriptorSetLayoutBinding, 6> bindings{};
        for (uint32_t i = 0; i < 6; ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        }

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 6;
        layoutInfo.pBindings = bindings.data();
        water.multiBandOceanLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Create pool
        vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler, 6};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        water.multiBandOceanPool = vkDevice.createDescriptorPool(poolInfo);

        // Allocate set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = water.multiBandOceanPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &water.multiBandOceanLayout;
        water.multiBandOceanDescSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

        // Update with band textures
        updateMultiBandOceanDescriptor();
    }

    void GPUDrivenRenderer::updateMultiBandOceanDescriptor()
    {
        if (!water.multiBandOceanDescSet)
            return;

        // Find a valid band to use as fallback for disabled bands
        render::water::OceanFFTResources* fallbackResources = nullptr;
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (water.oceanBands[i] && water.oceanBands[i]->isInitialized())
            {
                fallbackResources = water.oceanBands[i]->getResources();
                break;
            }
        }

        if (!fallbackResources ||
            !fallbackResources->getDisplacementView() ||
            !fallbackResources->getNormalView() ||
            !fallbackResources->getOutputSampler())
            return;

        std::array<vk::DescriptorImageInfo, 6> imageInfos{};

        for (uint32_t i = 0; i < 3; ++i)
        {
            render::water::OceanFFTResources* resources = fallbackResources;
            if (water.oceanBands[i] && water.oceanBands[i]->isInitialized())
                resources = water.oceanBands[i]->getResources();

            vk::Sampler sampler = resources->getOutputSampler();
            vk::ImageView dispView = resources->getDisplacementView();
            vk::ImageView normView = resources->getNormalView();

            // Safety: if views are null, use fallback
            if (!dispView) dispView = fallbackResources->getDisplacementView();
            if (!normView) normView = fallbackResources->getNormalView();
            if (!sampler) sampler = fallbackResources->getOutputSampler();

            imageInfos[i * 2 + 0] = {sampler, dispView, vk::ImageLayout::eShaderReadOnlyOptimal};
            imageInfos[i * 2 + 1] = {sampler, normView, vk::ImageLayout::eShaderReadOnlyOptimal};
        }

        // Write all 6 descriptors
        std::array<vk::WriteDescriptorSet, 6> writes{};
        for (uint32_t i = 0; i < 6; ++i)
        {
            writes[i].dstSet = water.multiBandOceanDescSet;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[i].pImageInfo = &imageInfos[i];
        }
        device.getLogicalDevice().updateDescriptorSets(writes, nullptr);
        water.multiBandDescriptorValid = true;
    }

    void GPUDrivenRenderer::cleanupOceanFFT()
    {
        water.oceanEnabled = false;

        // Clear caustic descriptor references from pipelines before destroying resources
        if (meshShaderPipeline) meshShaderPipeline->updateCausticDescriptor(vk::DescriptorSet{});
        if (transparentMeshShaderPipeline) transparentMeshShaderPipeline->updateCausticDescriptor(vk::DescriptorSet{});
        if (wboitMeshShaderPipeline) wboitMeshShaderPipeline->updateCausticDescriptor(vk::DescriptorSet{});
        if (terrain.pipeline)
        {
            terrain.pipeline->updateCausticDescriptor(vk::DescriptorSet{});
            terrain.pipeline->setCausticEnabled(false);
        }

        if (water.causticsResources)
        {
            water.causticsResources->cleanup();
            water.causticsResources.reset();
        }

        device.getLogicalDevice().waitIdle();

        // Cleanup all bands
        for (auto& band : water.oceanBands)
        {
            if (band)
            {
                band->cleanup();
                band.reset();
            }
        }
        water.activeBandCount = 0;

        // Cleanup composite descriptor
        vk::Device vkDevice = device.getLogicalDevice();
        if (water.multiBandOceanPool)
        {
            vkDevice.destroyDescriptorPool(water.multiBandOceanPool);
            water.multiBandOceanPool = nullptr;
        }
        if (water.multiBandOceanLayout)
        {
            vkDevice.destroyDescriptorSetLayout(water.multiBandOceanLayout);
            water.multiBandOceanLayout = nullptr;
        }
        water.multiBandOceanDescSet = nullptr;
        water.multiBandDescriptorValid = false;

        // Recreate mesh shader pipelines without caustic layout
        if (meshShaderPipeline && shadowSystem)
        {
            vk::DescriptorSetLayout giLayout{};
            if (giCascadeManager)
            {
                auto* storage = giCascadeManager->getProbeStorage();
                if (storage && storage->isInitialized())
                    giLayout = storage->getSamplingLayout();
            }

            MeshPipelineInitInfo pipelineInfo{
                .iblLayout = cachedIBLLayout,
                .bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout(),
                .boneMatrixLayout = boneMatrixManager->getDescriptorSetLayout(),
                .lightDataLayout = lightBufferManager->getDescriptorSetLayout(),
                .clusterGridLayout = clusterGridManager->getDescriptorSetLayout(),
                .cullingOutputLayout = lightCullingPipeline->getDescriptorSetLayout(),
                .shadowDataLayout = shadowSystem->getShadowDataLayout(),
                .shadowTextureLayout = shadowSystem->getShadowTextureLayout(),
                .giProbeDataLayout = giLayout,
                .causticLayout = nullptr,
                .worldMaskLayout = currentWorldMaskLayout(),
                .selectionCoverageLayout = selectionMaskPipeline ? selectionMaskPipeline->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
                .colorAttachmentFormats = cachedColorFormats,
                .depthAttachmentFormat = cachedDepthFormat
            };

            meshShaderPipeline->recreate(pipelineInfo);
            if (giLayout && giCascadeManager)
            {
                auto* storage = giCascadeManager->getProbeStorage();
                meshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
            }

            if (transparentMeshShaderPipeline)
            {
                pipelineInfo.transparentMode = true;
                transparentMeshShaderPipeline->recreate(pipelineInfo);
                if (giLayout && giCascadeManager)
                {
                    auto* storage = giCascadeManager->getProbeStorage();
                    transparentMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                }
                pipelineInfo.transparentMode = false;
            }

            if (wboitMeshShaderPipeline && !cachedWBOITColorFormats.empty())
            {
                pipelineInfo.colorAttachmentFormats = cachedWBOITColorFormats;
                pipelineInfo.depthAttachmentFormat = cachedWBOITDepthFormat;
                pipelineInfo.wboitMode = true;
                wboitMeshShaderPipeline->recreate(pipelineInfo);
                if (giLayout && giCascadeManager)
                {
                    auto* storage = giCascadeManager->getProbeStorage();
                    wboitMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                }
            }
        }

        // Recreate terrain pipeline without caustic layout
        if (terrain.pipeline)
        {
            terrain.pipeline->recreate(cachedIBLLayout,
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

        // Recreate water pipeline with dummy ocean layout
        if (water.pipeline)
        {
            vk::DescriptorSetLayout refractionLayout{};
            if (water.refractionResources && water.refractionResources->isInitialized())
                refractionLayout = water.refractionResources->getDescriptorSetLayout();

            water.pipeline->recreate({
                cachedIBLLayout,
                lightBufferManager->getDescriptorSetLayout(),
                clusterGridManager->getDescriptorSetLayout(),
                lightCullingPipeline->getDescriptorSetLayout(),
                shadowSystem->getShadowDataLayout(),
                shadowSystem->getShadowTextureLayout(),
                vk::DescriptorSetLayout{},
                refractionLayout,
                cachedColorFormats,
                cachedDepthFormat
            });
        }
    }

    void GPUDrivenRenderer::setOceanEnabled(bool enabled)
    {
        water.oceanEnabled = enabled;
        if (enabled)
        {
            water.activeBandCount = 0;
            for (auto& band : water.oceanBands)
                if (band && band->isInitialized()) water.activeBandCount++;
            water.oceanEnabled = water.activeBandCount > 0;
        }
    }

    void GPUDrivenRenderer::updateOceanConfig(const std::array<render::water::OceanFFTConfig, 3>& bandConfigs,
                                               const std::array<bool, 3>& bandEnabled)
    {
        bool needsRecreate = false;
        bool needsDescriptorRefresh = false;

        for (uint32_t i = 0; i < 3; ++i)
        {
            bool wasActive = water.oceanBands[i] && water.oceanBands[i]->isInitialized();

            if (bandEnabled[i] != wasActive)
            {
                needsRecreate = true;
            }
            else if (bandEnabled[i] && wasActive)
            {
                // Check if resolution changed (requires full band rebuild)
                bool resolutionChanged = bandConfigs[i].resolution != water.oceanBands[i]->getConfig().resolution;
                water.oceanBands[i]->updateConfig(bandConfigs[i]);
                if (resolutionChanged)
                    needsDescriptorRefresh = true;
            }
        }

        if (needsRecreate)
        {
            initOceanFFT(bandConfigs, bandEnabled);
        }
        else if (needsDescriptorRefresh)
        {
            // Resolution change rebuilt the band's resources — refresh composite descriptor
            updateMultiBandOceanDescriptor();
        }
    }

    void GPUDrivenRenderer::uploadShoreDepthField(vk::CommandBuffer cmd, float time)
    {
        // `time` is the same currentTime that fills CameraUBO::u_Time, so recording it here gives
        // the CPU shore-wave path the exact clock the vertex shader is running on.
        water.lastFrameTime = time;

        if (water.shoreDepthResources && water.shoreDepthResources->isInitialized())
            water.shoreDepthResources->recordUpload(cmd);
    }

    void GPUDrivenRenderer::queueWaterImpulses(const std::vector<::water::WaterImpulse>& impulses)
    {
        if (water.rippleSim)
            water.rippleSim->queueImpulses(impulses);
    }

    void GPUDrivenRenderer::dispatchWaterRipples(vk::CommandBuffer cmd, float time)
    {
        // Deliberately NOT gated on water.oceanEnabled: the ripple patch is independent of the FFT,
        // so ripple-only water (a pond, a flat-plane test scene) works with every band switched off.
        // The sim itself early-outs when its enabled flag is clear.
        if (water.rippleSim && water.rippleSim->isInitialized())
            water.rippleSim->dispatch(cmd, time);
    }

    void GPUDrivenRenderer::dispatchOceanFFT(vk::CommandBuffer cmd, float time)
    {
        if (!water.oceanEnabled)
            return;

        auto dispatchStart = std::chrono::high_resolution_clock::now();
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (water.oceanBands[i] && water.oceanBands[i]->isInitialized())
            {
                water.oceanBands[i]->dispatch(cmd, time);
                water.oceanBands[i]->insertBarrier(cmd);
            }
        }
        auto dispatchEnd = std::chrono::high_resolution_clock::now();
        water.dispatchUs = std::chrono::duration<float, std::micro>(dispatchEnd - dispatchStart).count();
    }

    void GPUDrivenRenderer::readbackOceanDisplacement()
    {
        if (!water.oceanEnabled)
            return;

        // VK-1604: decode EVERY initialized band, not just the first. Each band already records
        // its own GPU->CPU copy every frame inside OceanFFT::dispatch, so this adds only the F16C
        // decodes (~256 KB/frame for the two extra bands). Must stay in lock-step with
        // getOceanHeightAt below, which now sums all bands - summing a band that was never
        // decoded would silently contribute stale or zero height.
        auto readbackStart = std::chrono::high_resolution_clock::now();
        for (auto& band : water.oceanBands)
            if (band && band->isInitialized())
                band->readbackDisplacementData();
        auto readbackEnd = std::chrono::high_resolution_clock::now();
        water.readbackUs = std::chrono::duration<float, std::micro>(readbackEnd - readbackStart).count();
    }

    uint32_t GPUDrivenRenderer::waterTileLodAt(const glm::vec2& worldXZ) const
    {
        const float tileSize = water.lodTileSize > 0.0f ? water.lodTileSize : 1.0f;

        if (water.lodWorldMode)
        {
            // Same measurement buildGPUTileData takes: camera to TILE CENTRE, not to the sample
            // point, so every point inside one tile resolves to that tile's LOD.
            const glm::vec2 tileOrigin(std::floor(worldXZ.x / tileSize) * tileSize,
                                       std::floor(worldXZ.y / tileSize) * tileSize);
            const glm::vec2 tileCenter = tileOrigin + glm::vec2(tileSize * 0.5f);
            return ::water::selectTileLod(glm::length(tileCenter - water.lodCameraXZ), tileSize);
        }

        // Editor mode: Chebyshev ring around the camera's snapped centre tile, exactly as the 9x9
        // grid is bucketed in updateWater. Past the grid's edge the answer is the coarsest LOD,
        // which is also what the outermost ring carries.
        const int tx = static_cast<int>(std::floor((worldXZ.x - water.lodGridOriginXZ.x) / tileSize));
        const int tz = static_cast<int>(std::floor((worldXZ.y - water.lodGridOriginXZ.y) / tileSize));
        return ::water::editorRingLod(std::max(std::abs(tx), std::abs(tz)));
    }

    float GPUDrivenRenderer::getOceanHeightAt(const glm::vec2& worldXZ) const
    {
        if (!water.oceanEnabled)
            return 0.0f;

        // VK-1604: sum ALL initialized bands. Previously this returned the first initialized
        // band's height, so agitation and ripple waves never moved floating bodies and the
        // physics surface sat below the rendered one.
        //
        // Hex-tiled bands are blended here exactly as water.glsl blends them (same lattice, same
        // integer hash, same variance-preserving combine), so buoyancy keeps matching the
        // rendered surface on whichever bands are tiled. Costs 3x the bilinear taps, but only
        // for the bands the user actually enabled.
        // VK-1605: shoaling and the breaking-wave surge are applied here with the same math and the
        // same inputs the vertex shader used - same shore depth, same window fade, same per-band
        // wavelength, and the same frame time that drove camera.u_Time. Anything less and a floating
        // body sits at a different height than the water it is drawn in.
        const bool shoreActive = water.shoalingEnabled || water.shoreWavesEnabled;
        float shoreDepth = ::water::SHORE_FIELD_DEEP;
        float shoreFade = 0.0f;
        if (shoreActive)
        {
            shoreDepth = ::water::sampleShoreDepth(water.shoreDepthData, water.shoreFieldResolution,
                                                   water.shoreFieldOrigin, water.shoreFieldWindow,
                                                   worldXZ);
            shoreFade = ::water::shoreWindowFade(worldXZ, water.shoreFieldOrigin,
                                                 water.shoreFieldWindow, water.shoreEdgeFadeStart);
        }
        const float shoalStrength = water.shoalingEnabled ? water.shoalingStrength * shoreFade : 0.0f;

        // VK-1607: apply the same per-tile band LOD the vertex stage applies. Summing every band
        // regardless of distance is what let a body on a ring-3 tile bob on agitation and ripple
        // waves the shader had already culled from the geometry it was drawn against. See
        // water::lodBandMask (and its documented world-mode caveat) for the shared rule.
        const uint32_t bandMask = ::water::lodBandMask(::water::WATER_TILE_BAND_MASK_BITS,
                                                       waterTileLodAt(worldXZ));

        float height = 0.0f;
        for (uint32_t i = 0; i < static_cast<uint32_t>(water.oceanBands.size()); ++i)
        {
            const auto& band = water.oceanBands[i];
            if (!band || !band->isInitialized())
                continue;
            if ((bandMask & (1u << i)) == 0u)
                continue;

            float bandHeight;
            if (water.hexTilingEnabled && ::water::hexBandEnabled(water.hexBandMask, i))
            {
                const float patchSize = band->getConfig().patchSize;
                const glm::vec2 uv = ::water::patchUV(worldXZ, patchSize);
                const ::water::HexBlend hb =
                    ::water::hexComputeBlend(uv, water.hexCellScale, water.hexBlendContrast);
                bandHeight = ::water::hexCombineVariancePreserving(hb,
                    band->sampleHeightAtUV(hb.uv[0]),
                    band->sampleHeightAtUV(hb.uv[1]),
                    band->sampleHeightAtUV(hb.uv[2]));
            }
            else
            {
                bandHeight = band->sampleHeightAt(worldXZ);
            }

            if (shoalStrength > 0.0f)
            {
                bandHeight *= ::water::shoalingScale(shoreDepth,
                                                     water.bandWavelength[static_cast<int>(i)],
                                                     bandHeight, shoalStrength,
                                                     water.shoalingGamma, water.shoalingMinDepth);
            }

            height += bandHeight;
        }

        if (water.shoreWavesEnabled)
            height += ::water::shoreWaveHeight(shoreDepth, water.lastFrameTime,
                                               water.shoreWaveParams) * shoreFade;

        return height;
    }

    void GPUDrivenRenderer::copySceneColorForRefraction(vk::CommandBuffer cmd, vk::Image colorImage,
                                                         uint32_t width, uint32_t height)
    {
        if (water.refractionResources && water.refractionResources->isInitialized())
            water.refractionResources->copySceneColor(cmd, colorImage, width, height);
    }

    void GPUDrivenRenderer::recreateRefractionResources(vk::ImageView sceneDepthView)
    {
        if (!water.refractionResources)
            return;

        // VK-1605/VK-1606: recreate() tears the whole set 9 down, so bindings 3 and 4 have to be
        // handed back in.
        vk::ImageView shoreView{};
        vk::Sampler shoreSampler{};
        if (water.shoreDepthResources && water.shoreDepthResources->isInitialized())
        {
            shoreView = water.shoreDepthResources->getImageView();
            shoreSampler = water.shoreDepthResources->getSampler();
        }

        vk::ImageView rippleView{};
        vk::Sampler rippleSampler{};
        if (water.rippleSim && water.rippleSim->isInitialized())
        {
            rippleView = water.rippleSim->getOutputView();
            rippleSampler = water.rippleSim->getSampler();
        }

        water.refractionResources->recreate(
            swapChain.getSceneColorFormat(),
            swapChain.getSwapchainExtent().width,
            swapChain.getSwapchainExtent().height,
            sceneDepthView,
            shoreView, shoreSampler,
            rippleView, rippleSampler);
    }
}
