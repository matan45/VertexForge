#pragma once

// VK-1443: per-subsystem state structs for GPUDrivenRenderer, extracted out of
// GPUDrivenRenderer.hpp to slim that header. These are purely the private nested
// state aggregates; they carry no behavior. Promoted to render::gpudriven::detail
// (still nested inside render::gpudriven, so the unqualified type lookups the
// structs relied on while nested in GPUDrivenRenderer continue to resolve).

#include "GPUDrivenTypes.hpp"
#include "terrain/TerrainMeshShaderPipeline.hpp"
#include "terrain/TerrainMeshBuffer.hpp"
#include "terrain/TerrainGPUAdapter.hpp"
#include "terrain/TerrainStreamManager.hpp"
#include "../water/WaterPipeline.hpp"
#include "../water/WaterMeshBuffer.hpp"
#include "../water/WaterGPUTypes.hpp"
#include "../water/OceanFFT.hpp"
#include "../water/WaterRefractionResources.hpp"
#include "../water/WaterCausticsResources.hpp"
#include "../vegetation/WindSystem.hpp"
#include "../vegetation/VegetationBufferManager.hpp"
#include "../vegetation/GrassStreamManager.hpp"
#include "billboard/BillboardBufferManager.hpp"
#include "billboard/BillboardMeshShaderPipeline.hpp"
#include "billboard/BillboardGPUTypes.hpp"
#include "billboard/BillboardStreamManager.hpp"
#include "vegetation/GrassConfig.hpp"
#include "vegetation/VegetationTypes.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "material/MaterialManager.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <array>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace material
{
    struct MaterialData;
}

namespace render::mesh
{
    class MaterialTextureCache;
}

namespace terrain
{
    class TerrainTile;
}

namespace render::vegetation
{
    class GrassMeshShaderPipeline;
    class WindSystem;
}

namespace render::gpudriven::detail
{
    struct TerrainState
    {
        std::unique_ptr<TerrainMeshBuffer> meshBuffer;
        std::unique_ptr<TerrainMeshShaderPipeline> pipeline;
        std::unique_ptr<TerrainGPUAdapter> adapter;
        std::unique_ptr<TerrainStreamManager> streamManager;
        std::vector<TerrainTileGPUData> tileData;
        bool renderingEnabled = true;
        float lodBias = 1.0f;
        float errorThreshold = 2.0f;
        float textureScale = 0.1f;
        std::string currentMaterialPath;
        std::vector<TerrainLayerGPUData> layerData;
        bool layerDataDirty = false;
        float updateUs = 0.0f;
        float streamingUs = 0.0f;
        float buildTileDataUs = 0.0f;
        float uploadTileDataUs = 0.0f;
        TerrainStreamManager::TileDataLoader pendingTileDataLoader;
        TerrainStreamManager::TileRAMEvictor pendingTileRAMEvictor;
        TerrainStreamManager::TileLoadContextProvider pendingTileLoadContextProvider;
    };

    struct WaterState
    {
        std::unique_ptr<render::water::WaterPipeline> pipeline;
        std::unique_ptr<render::water::WaterMeshBuffer> meshBuffer;
        std::vector<render::water::WaterTileGPUData> tileData;
        uint32_t lodTileCounts[render::water::WATER_LOD_COUNT] = {};
        render::water::WaterPushConstants cachedPushConstants{};
        bool renderingEnabled = true;
        // Ocean FFT (multi-band)
        static constexpr uint32_t MAX_OCEAN_BANDS = 3;
        std::array<std::unique_ptr<render::water::OceanFFT>, MAX_OCEAN_BANDS> oceanBands;
        uint32_t activeBandCount = 0;
        bool oceanEnabled = false;

        // Composite descriptor set for multi-band ocean textures (6 bindings)
        vk::DescriptorSetLayout multiBandOceanLayout;
        vk::DescriptorPool multiBandOceanPool;
        vk::DescriptorSet multiBandOceanDescSet;
        bool multiBandDescriptorValid = false;
        // Refraction
        std::unique_ptr<render::water::WaterRefractionResources> refractionResources;
        // Caustics
        std::unique_ptr<render::water::WaterCausticsResources> causticsResources;

        // Timing (microseconds)
        float readbackUs = 0.0f;
        float dispatchUs = 0.0f;
        float updateUs = 0.0f;
        float renderUs = 0.0f;
    };

    struct VegetationState
    {
        // Compute pipeline removed - instances uploaded directly
        std::unique_ptr<render::vegetation::GrassMeshShaderPipeline> grassMeshPipeline;
        std::unique_ptr<render::vegetation::WindSystem> windSystem;

        // Buffer manager for vegetation tile allocations
        std::unique_ptr<render::vegetation::VegetationBufferManager> bufferManager;

        // Stream managers
        std::unique_ptr<render::vegetation::GrassStreamManager> grassStreamManager;

        // Grass instance buffer (GPU-side output from compute pipeline)
        vk::Buffer grassInstanceBuffer;
        core::VulkanAllocation grassInstanceBufferAllocation;
        vk::Buffer grassCounterBuffer;
        core::VulkanAllocation grassCounterBufferAllocation;
        uint32_t grassInstanceCapacity = 0;
        uint32_t currentGrassInstanceCount = 0;

        bool grassRenderingEnabled = true;
        bool grassInitialized = false;

        ::vegetation::GrassRenderConfig grassConfig;

        // Billboard palette: resolved entries
        struct BillboardGPUEntry
        {
            uint32_t bindlessIndex = 0xFFFFFFFF;
            uint32_t mode = 0;
            float weight = 1.0f;
            float scaleMin = 0.0f;
            float scaleMax = 0.0f;
            bool visible = true;
        };
        std::vector<BillboardGPUEntry> billboardPalette;
        int32_t activeBillboardEntry = -1; // -1 = All (Random), >= 0 = specific entry

        // Palette loader callback (for auto-load on scene load)
        std::function<std::vector<::vegetation::BillboardPaletteEntry>()> billboardPaletteLoader;

        // Instance staging buffer for direct upload
        vk::Buffer instanceStagingBuffer;
        core::VulkanAllocation instanceStagingAllocation;
        void* instanceStagingMapped = nullptr;
        uint32_t instanceStagingCapacity = 0;

        std::vector<vk::Format> cachedColorFormats;
        vk::Format cachedDepthFormat = vk::Format::eUndefined;

        // Track which terrain tiles have vegetation registered
        std::unordered_set<uint64_t> registeredTileKeys;

        // Cached visible tiles for compute dispatch (set during updateVegetationStreaming)
        std::vector<terrain::TerrainTile*> cachedVisibleTiles;

    };

    struct BillboardState
    {
        std::unique_ptr<BillboardBufferManager> bufferManager;
        std::unique_ptr<BillboardMeshShaderPipeline> meshShaderPipeline;
        std::unique_ptr<BillboardStreamManager> streamManager;
        std::vector<BillboardInstanceGPU> instanceList;
        BillboardRenderStats stats;
        bool renderingEnabled = true;
        bool initialized = false;
    };

    struct LightCullingState
    {
        std::unordered_set<uint32_t> visibleLightIds;
        bool useBVH = false;
        bool useOcclusion = false;
        std::unordered_set<uint32_t> prevFrameOccludedLights;
        bool hasPrevFrameOcclusionData = false;
        uint32_t totalSceneLights = 0;
        uint32_t lightsAfterBVHCull = 0;
        uint32_t lightsAfterHiZCull = 0;
    };

    struct MaterialState
    {
        mesh::MaterialTextureCache* textureCache = nullptr;
        std::unordered_set<std::string> registeredPaths;
        std::unordered_map<std::string, std::shared_ptr<material::MaterialData>> loaded;
        std::unordered_map<std::string, mesh::ExtractedPBRValues> pbrCache;
        material::CallbackId changeCallbackId{};
    };

    struct CullingConfig
    {
        bool frustumCullingEnabled = true;
        bool lodSelectionEnabled = true;
        bool lodCrossfadeEnabled = true;
        bool occlusionCullingEnabled = true;
        bool distanceCullingEnabled = false;
        float categoryDistances[services::CullingCategory::Count] = {1000.0f, 2000.0f, 500.0f, 300.0f, 200.0f, 500.0f, 1000.0f};
        float shadowDistanceMultiplier = 0.5f;
        float globalLodBias = 0.0f;
        bool meshletFrustumCullingEnabled = true;
        bool meshletBackfaceCullingEnabled = true;
        bool meshletOcclusionCullingEnabled = true;
        uint32_t currentViewMode = 0;
    };

    struct CachedCamera
    {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec3 position{0.0f};
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float time = 0.0f;
    };
}
