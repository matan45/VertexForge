#pragma once
#include "DebugFrameBuilder.hpp"
#include "UIFrameBuilder.hpp"
#include "UIInteractionSystem.hpp"
#include "UIAnimationSystem.hpp"
#include "../../render/material/MaterialPBRExtractor.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include "../../render/billboard/BillboardTypes.hpp"
#include "../../render/text/TextTypes.hpp"
#include "../../../services/providers/render/IDecalRenderProvider.hpp"
#include "terrain/TerrainTypes.hpp" // VK-1573: TileCoord/TileCoordHash for the foliage draw cache key
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace render
{
    class RenderPassHandler;
}

namespace controllers::offscreen
{
    class SceneBVHManager;
    class LightBVHManager;
    class CameraController;

    struct FrameContext
    {
        render::RenderPassHandler* renderHandler = nullptr;
        SceneBVHManager* bvhManager = nullptr;
        LightBVHManager* lightBvhManager = nullptr;
        CameraController* cameraController = nullptr;
        bool playModeActive = false;
        // VK-1442 — editor-only: the UI Layer Builder's scoped offscreen preview sets this so the
        // interaction-driven parts of compound widgets (checkbox skin, tabs active pane, expanded
        // dropdown list, text tooltip bubble) render for authoring WITHOUT any hit-testing. The
        // runtime prepareUICanvas path never sets it, so default false ⇒ runtime byte-identical.
        bool editPreview = false;
        bool showDebugRendering = true;
        bool showBillboardIcons = true;
        bool showGrid = true;
        bool showPhysicsDebug = false;
        bool showClusterDebug = false;
        float deltaTime = 0.0f;          // raw/unscaled — UI systems (caret blink, tooltips, transitions)
        float gameplayDeltaTime = 0.0f;  // VK-992: scaled gameplay delta — animator playback (0 when frozen)
        uint32_t viewportWidth = 0;
        uint32_t viewportHeight = 0;
        glm::vec2 mousePosition{0.0f, 0.0f};
        glm::vec2 scrollDelta{0.0f, 0.0f};
        bool leftMouseDown = false;
        bool leftMousePressed = false;   // rising edge (was up, now down)
        bool leftMouseReleased = false;  // falling edge (was down, now up)
        bool leftMouseDoubleClick = false;

        // Keyboard/text input data (for UITextInput interaction)
        std::vector<uint32_t> charInput;
        // Key state query function (key code -> pressed this frame)
        // Uses GLFW key codes as integer values
        std::function<bool(int)> isKeyPressed;
        std::function<bool(int)> isKeyDown;
        std::function<std::string()> getClipboardText;
        std::function<void(const std::string&)> setClipboardText;
    };

    class FramePreparationSystem
    {
    private:
        std::unordered_map<std::string, render::mesh::ExtractedPBRValues> pbrCache;

        // Cache: material instance path → {parentMaterialPath, hasTextureOverrides}
        struct InstanceBatchInfo
        {
            std::string parentMaterialPath;
            bool hasTextureOverrides = false;
        };
        std::unordered_map<std::string, InstanceBatchInfo> instanceBatchCache;

        // VK-1573: persistent per-tile composed foliage transforms. One ComposedFoliageDraw per
        // (tile, foliage-type present); rebuilt only when a tile's foliageInstancesGPUDirty flag
        // is set, then emitted as pre-instanced MeshRenderData records each frame. Lives on the
        // collector (not on the ABI-frozen TerrainTile) — see collectFoliage().
        struct ComposedFoliageDraw
        {
            uint16_t typeIndex = 0;
            std::vector<render::mesh::MeshRenderData::InstanceData> transforms;
        };
        std::unordered_map<terrain::TileCoord, std::vector<ComposedFoliageDraw>, terrain::TileCoordHash> foliageDrawCache;

        // VK-1582: FNV hash of the inputs to the density-scale subset selection (global scale +
        // every palette entry's densityScale/affectedByDensityScale). setFoliagePalette does NOT
        // GPU-dirty tiles, so when this hash changes the whole foliage draw cache is dropped and
        // re-composed with the new survivor set. uint32 hash avoids float-accumulation overflow.
        uint32_t foliageDensitySignature = 0u;

        DebugFrameBuilder debugBuilder;
        UIFrameBuilder uiFrameBuilder;
        UIInteractionSystem uiInteraction;
        UIAnimationSystem uiAnimation;

        const render::mesh::ExtractedPBRValues* getCachedPBRValues(const std::string& materialPath);
        // Override-aware variant: caches per (materialPath, override-hash) so entities
        // with distinct runtime parameter values don't thrash one cache slot
        const render::mesh::ExtractedPBRValues* getCachedPBRValues(
            const std::string& materialPath,
            const render::mesh::MaterialPBRExtractor::ParameterOverrides* runtimeOverrides);
        void populateMaterialInfo(render::mesh::SubMeshMaterialInfo& matInfo, const std::string& materialPath,
                                  const render::mesh::MaterialPBRExtractor::ParameterOverrides* runtimeOverrides = nullptr);

        // VK-1573: walk resident terrain tiles and append one pre-instanced MeshRenderData per
        // (tile, foliage-type) into meshDrawList, feeding the existing GPU-instancing path.
        void collectFoliage(std::vector<render::mesh::MeshRenderData>& meshDrawList,
                            const FrameContext& ctx, const glm::vec3& cameraPos);

    public:
        FramePreparationSystem() = default;

        void prepareMeshes(const FrameContext& ctx);
        void prepareBillboards(const FrameContext& ctx);
        void prepareText(const FrameContext& ctx);

        // Parallel variant: runs billboard and text data gathering concurrently
        // with mesh preparation. Vulkan pipeline init stays on main thread.
        void prepareSceneData(const FrameContext& ctx);

    private:
        std::vector<render::billboard::BillboardRenderData> gatherBillboardData(const FrameContext& ctx);
        std::vector<render::text::TextRenderData> gatherTextData(const FrameContext& ctx);
        // Phase 2: gather worldMarker billboards and feed the GPU mesh-shader path.
        void prepareGPUBillboards(const FrameContext& ctx);
        void prepareDecals(const FrameContext& ctx);

    public:

        void prepareCameraFrustums(const FrameContext& ctx) { debugBuilder.prepareCameraFrustums(ctx); }
        void prepareAudioSpheres(const FrameContext& ctx) { debugBuilder.prepareAudioSpheres(ctx); }
        void prepareReverbZones(const FrameContext& ctx) { debugBuilder.prepareReverbZones(ctx); }
        void prepareFogVolumes(const FrameContext& ctx) { debugBuilder.prepareFogVolumes(ctx); }
        void prepareReflectionProbes(const FrameContext& ctx) { debugBuilder.prepareReflectionProbes(ctx); }
        void prepareGrid(const FrameContext& ctx) { debugBuilder.prepareGrid(ctx); }
        void preparePhysicsColliders(const FrameContext& ctx) { debugBuilder.preparePhysicsColliders(ctx); }
        void prepareLightGizmos(const FrameContext& ctx) { debugBuilder.prepareLightGizmos(ctx); }
        void prepareClusterDebug(const FrameContext& ctx) { debugBuilder.prepareClusterDebug(ctx); }
        void prepareUICanvasOutlines(const FrameContext& ctx) { debugBuilder.prepareUICanvasOutlines(ctx); }

        void prepareUIImages(const FrameContext& ctx) { uiFrameBuilder.prepareUIImages(ctx, uiInteraction, uiAnimation); }
        void prepareUILabels(const FrameContext& ctx) { uiFrameBuilder.prepareUILabels(ctx); }

        void invalidateMaterialCache(const std::string& materialPath);
        void clearAllMaterialCache();
    };
}
