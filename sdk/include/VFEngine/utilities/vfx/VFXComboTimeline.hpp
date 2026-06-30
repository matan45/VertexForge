#pragma once

#include "VFXSequenceTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace vfx
{
    enum class ComboEventKind : uint8_t
    {
        SpawnStep,
        StopStep
    };

    // A scheduling decision the timeline made at the current simulated time.
    struct ComboEvent
    {
        ComboEventKind kind;
        int stepIndex;
    };

    // VK-1451 — the single source of truth for "which combo steps spawn/stop/fire at
    // simulated time t, with which derived seed and local transform". Pure, entt-free,
    // header-only and CPU-only, so it is shared by the runtime combo service AND the
    // editor composited preview (no duplicated scheduling logic), and is directly
    // unit-testable without a dispatcher, Vulkan device, or GLFW window.
    //
    // Determinism: advance() is a pure function of (data, seed, dt). With the caller's
    // fixed-step accumulator the spawn/stop set after reaching time T is identical no
    // matter how the wall-clock dt was chunked. rewind() restores t=0 so seek/prewarm
    // re-derive the same schedule. No RNG lives here — only the deterministic per-step
    // seed derivation handed to child instances.
    //
    // Lifetime: reset() stores a non-owning pointer to `data`; the caller must keep the
    // VFXSequenceData alive for as long as the timeline is used.
    class VFXComboTimeline
    {
    public:
        // Bind the schedule to a sequence and effective seed. `effectiveSeed` should be
        // the already-resolved combo seed (0 is allowed but downstream derivation maps
        // it to a stable non-zero value).
        void reset(const VFXSequenceData& data, uint32_t effectiveSeed)
        {
            data_ = &data;
            seed_ = effectiveSeed;
            spawned_.assign(data.steps.size(), false);
            stopped_.assign(data.steps.size(), false);
            firedMarkers_.assign(data.eventMarkers.size(), false);
            elapsed_ = 0.0f;
        }

        // Return to t=0 and clear all crossing guards (for seek / prewarm replay).
        void rewind()
        {
            std::fill(spawned_.begin(), spawned_.end(), false);
            std::fill(stopped_.begin(), stopped_.end(), false);
            std::fill(firedMarkers_.begin(), firedMarkers_.end(), false);
            elapsed_ = 0.0f;
        }

        // Advance simulated time by dt and append the spawn/stop events crossed in
        // (elapsed, elapsed+dt]. Order within one call mirrors the Phase-1 runtime loop:
        // time-driven spawns, then marker-fired cue spawns, then StopAfterDuration stops.
        void advance(float dt, std::vector<ComboEvent>& out)
        {
            if (!data_ || dt <= 0.0f)
                return;
            elapsed_ += dt;

            // 1) Time-driven steps (empty cueName) whose start time has arrived.
            for (size_t i = 0; i < data_->steps.size(); ++i)
            {
                const VFXSequenceStep& s = data_->steps[i];
                if (spawned_[i] || !s.cueName.empty())
                    continue;
                if (elapsed_ >= s.startTime)
                {
                    spawned_[i] = true;
                    out.push_back({ComboEventKind::SpawnStep, static_cast<int>(i)});
                }
            }

            // 2) One-shot event markers fire their named cue exactly once when crossed.
            for (size_t m = 0; m < data_->eventMarkers.size(); ++m)
            {
                if (firedMarkers_[m])
                    continue;
                const VFXSequenceEventMarker& marker = data_->eventMarkers[m];
                if (elapsed_ >= marker.time)
                {
                    firedMarkers_[m] = true;
                    fireCueInternal(marker.cueName, out);
                }
            }

            // 3) StopAfterDuration steps whose window has ended.
            emitDueStops(out);
        }

        // Manually fire a named cue (TriggerVFXComboCue / editor cue button). Spawns
        // every not-yet-spawned step whose cueName matches.
        void fireCue(std::string_view cueName, std::vector<ComboEvent>& out)
        {
            if (!data_)
                return;
            fireCueInternal(cueName, out);
            // A cue step with an already-elapsed StopAfterDuration window stops at once.
            emitDueStops(out);
        }

        uint32_t derivedSeed(int stepIndex) const { return deriveSeed(seed_, stepIndex); }

        glm::mat4 localTransform(int stepIndex) const
        {
            return composeStepLocal(data_->steps[static_cast<size_t>(stepIndex)]);
        }

        const VFXSequenceStep& step(int stepIndex) const
        {
            return data_->steps[static_cast<size_t>(stepIndex)];
        }

        float elapsed() const { return elapsed_; }
        uint32_t seed() const { return seed_; }
        int stepCount() const { return data_ ? static_cast<int>(data_->steps.size()) : 0; }

        bool isSpawned(int stepIndex) const { return spawned_[static_cast<size_t>(stepIndex)]; }
        bool isStopped(int stepIndex) const { return stopped_[static_cast<size_t>(stepIndex)]; }

        // Mirrors the Phase-1 completion gate: every step (time- and cue-driven) spawned.
        bool allStepsSpawned() const
        {
            return std::all_of(spawned_.begin(), spawned_.end(), [](bool b) { return b; });
        }

        // Local TRS of a step (lifted verbatim from the Phase-1 runtime service).
        static glm::mat4 composeStepLocal(const VFXSequenceStep& step)
        {
            const glm::mat4 t = glm::translate(glm::mat4(1.0f), step.localPosition);
            const glm::mat4 r = glm::mat4_cast(glm::quat(glm::radians(step.localEulerDeg)));
            const glm::mat4 s = glm::scale(glm::mat4(1.0f), step.localScale);
            return t * r * s;
        }

        // Stable, deterministic per-step seed (PCG-style finalizer mix). Never returns 0
        // (0 is reserved for "auto-random" upstream), so a derived seed is always usable.
        static uint32_t deriveSeed(uint32_t comboSeed, int stepIndex)
        {
            uint32_t h = (comboSeed ? comboSeed : 0x9E3779B9u) ^
                         (static_cast<uint32_t>(stepIndex) * 0x85EBCA6Bu + 0xC2B2AE35u);
            h ^= h >> 16;
            h *= 0x7FEB352Du;
            h ^= h >> 15;
            h *= 0x846CA68Bu;
            h ^= h >> 16;
            return h ? h : 1u;
        }

    private:
        void fireCueInternal(std::string_view cueName, std::vector<ComboEvent>& out)
        {
            if (cueName.empty())
                return;
            for (size_t i = 0; i < data_->steps.size(); ++i)
            {
                const VFXSequenceStep& s = data_->steps[i];
                if (spawned_[i] || s.cueName.empty() || std::string_view(s.cueName) != cueName)
                    continue;
                spawned_[i] = true;
                out.push_back({ComboEventKind::SpawnStep, static_cast<int>(i)});
            }
        }

        void emitDueStops(std::vector<ComboEvent>& out)
        {
            for (size_t i = 0; i < data_->steps.size(); ++i)
            {
                const VFXSequenceStep& s = data_->steps[i];
                if (!spawned_[i] || stopped_[i])
                    continue;
                if (s.stopMode == VFXStepStopMode::StopAfterDuration && s.duration > 0.0f &&
                    elapsed_ >= s.startTime + s.duration)
                {
                    stopped_[i] = true;
                    out.push_back({ComboEventKind::StopStep, static_cast<int>(i)});
                }
            }
        }

        const VFXSequenceData* data_ = nullptr;
        uint32_t seed_ = 0;
        float elapsed_ = 0.0f;
        std::vector<bool> spawned_;
        std::vector<bool> stopped_;
        std::vector<bool> firedMarkers_;
    };
}
