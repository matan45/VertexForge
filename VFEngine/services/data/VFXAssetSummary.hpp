#pragma once

#include <vfx/VFXBlendMode.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// VK-1657 — what the MCP VFX tools report about a .vfVFX effect or a .vfVFXSequence combo: the
// AUTHORED values read from the file (VFXAssetSummaryService), before any per-entity override.
namespace services
{
    // One enabled particle event of an effect's emitter.
    struct VFXEventSummary
    {
        std::string type;      // "spawn" | "death" | "collision" | "lifetimeThreshold"
        bool notify = false;   // also delivered to IVFXEventListener scripts
        std::string subEffect; // .vfVFX spawned at the event ("" = none)
    };

    struct VFXEffectSummary
    {
        float spawnRate = 0.0f;
        float lifetime = 0.0f;
        float startSize = 0.0f;
        float startSpeed = 0.0f;
        glm::vec4 startColor{1.0f};
        bool looping = true;
        uint32_t burstCount = 0;
        // A ColorOverLifetime node replaces the particle colour every frame: birthColor / deathColor
        // are its gradient at age 0 / 1, otherwise both equal startColor.
        bool colorOverLifetime = false;
        glm::vec4 birthColor{1.0f};
        glm::vec4 deathColor{1.0f};
        vfx::VFXBlendMode blendMode = vfx::VFXBlendMode::Alpha; // "blendMode" string, else legacy additiveBlend
        int32_t sortOrder = 0;
        int32_t renderMode = 0; // vfxRenderModeName
        bool emitsLight = false;
        std::string texture;
        std::vector<VFXEventSummary> events;
    };

    struct VFXComboStepSummary
    {
        std::string label;
        std::string kind;       // "vfx" | "sound" | "scriptCue"
        std::string effectPath; // VFX steps: the referenced .vfVFX ("" when unresolved)
        float startTime = 0.0f;
        std::string cue;        // cue that spawns the step ("" = time-driven)
        bool loop = false;
    };

    struct VFXComboSummary
    {
        std::vector<VFXComboStepSummary> steps;
        std::vector<std::string> cues; // every cue the combo reacts to (steps, markers) or emits (ScriptCue)
        float playbackRate = 1.0f;
    };

    struct VFXAssetSummary
    {
        std::string path; // as requested
        bool isCombo = false;
        bool valid = false;
        std::string error;
        std::string name;
        std::optional<VFXEffectSummary> effect;
        std::optional<VFXComboSummary> combo;
    };

    // Names of render::vfx::VFXRenderMode (graphics/render/vfx/billboard/VFXBillboardTypes.hpp).
    inline const char* vfxRenderModeName(int32_t renderMode)
    {
        switch (renderMode)
        {
        case 0:  return "Billboard";
        case 1:  return "StretchedBillboard";
        case 2:  return "HorizontalBillboard";
        case 3:  return "MeshParticle";
        case 4:  return "Ribbon";
        default: return "Billboard";
        }
    }
}
