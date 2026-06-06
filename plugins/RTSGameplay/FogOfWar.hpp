#pragma once

#include "api/PluginContext.hpp"
#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

// Fog of war (VK-1314): per-frame player-team visibility grid streamed to the GPU
// as an R8 world-space mask via the VK-1359 plugin texture API. Terrain dims and
// entities hide outside the player's combined vision circles.

// Fog settings + stats shared between the system and its editor window. Held by
// shared_ptr on both sides so destruction order doesn't matter (HexTerrain pattern).
struct FogSettings
{
    bool enabled = true;               // runtime gate (F10 / UI checkbox)
    float terrainDimMin = 0.35f;       // unseen terrain albedo multiplier
    float entityDiscardBelow = 0.5f;   // hide entities where mask < threshold
    bool paramsDirty = false;          // UI/F10 changed something -> re-push params

    // Stats (written by the system, read by the UI)
    int visionSources = 0;
    float gridUpdateMs = 0.0f;
    bool bound = false;                // mask bound to the renderer
};

class FogOfWarSystem
{
public:
    // Creates the GPU mask texture (graphics capability required). Safe to call
    // without the capability — the system stays inert.
    void initialize(plugin::PluginContext* context, std::shared_ptr<FogSettings> sharedSettings);

    // Per-frame: handles the F10 toggle, rebuilds the visibility grid from
    // Vision-bearing player entities, streams it to the GPU, and (re)applies
    // mask params when settings changed. Lazily binds the world mask on the
    // first frame that has a vision source.
    void update();

    // Unbinds the mask and destroys the texture (also covered by the engine's
    // plugin-unload auto-cleanup, but explicit is better).
    void shutdown();

private:
    // Two-state fog of war (visible / unseen) over the skirmish map bounds.
    // Bounds match the demo's RTSCameraController / BuildingPlacementController.
    static constexpr uint32_t FOG_GRID = 256;          // 2 m per cell over the 512 m map
    static constexpr float MAP_MIN = -256.0f;
    static constexpr float MAP_MAX = 256.0f;

    plugin::WorldMaskParams makeFogParams(bool enabled) const;
    void updateVisibilityGrid();
    void stampVisionCircle(float worldX, float worldZ, float radius, float invCellSize);

    static float elapsedMs(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    plugin::PluginContext* ctx = nullptr;
    std::shared_ptr<FogSettings> settings;

    plugin::PluginTextureHandle fogTexture;
    std::vector<std::byte> visibilityGrid;
    bool fogBound = false;        // mask bound to the renderer
    bool fogActive = false;       // enabled flag currently set in the params UBO
};
