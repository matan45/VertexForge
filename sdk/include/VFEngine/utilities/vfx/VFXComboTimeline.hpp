#pragma once

#include "VFXSequenceTypes.hpp"
#include "VFXVariance.hpp"
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
        int sourceMarker = -1;
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
            plays_ = resolvePlays(data, effectiveSeed); // VK-1497 — resolve per-step variety once
        }

        // Return to t=0 and clear all crossing guards (for seek / prewarm replay).
        void rewind()
        {
            std::fill(spawned_.begin(), spawned_.end(), false);
            std::fill(stopped_.begin(), stopped_.end(), false);
            std::fill(firedMarkers_.begin(), firedMarkers_.end(), false);
            elapsed_ = 0.0f;
            // VK-1497 — re-resolve identically (rewind() leaves seed_ untouched), so seek /
            // prewarm / replay reproduce the exact same play/skip choices as forward playback.
            if (data_)
                plays_ = resolvePlays(*data_, seed_);
        }

        // Advance simulated time by dt and append the spawn/stop events crossed in
        // (elapsed, elapsed+dt]. Order within one call mirrors the Phase-1 runtime loop:
        // time-driven spawns, then marker-fired cue spawns, then StopAfterDuration stops.
        void advance(float dt, std::vector<ComboEvent>& out)
        {
            if (!data_ || dt < 0.0f)
                return;
            elapsed_ += dt;

            // 1) Time-driven steps (empty cueName) whose start time has arrived.
            for (size_t i = 0; i < data_->steps.size(); ++i)
            {
                const VFXSequenceStep& s = data_->steps[i];
                if (spawned_[i] || !s.cueName.empty() || !plays_[i]) // VK-1497 — skip resolved-out steps
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
                    fireCueInternal(marker.cueName, out, static_cast<int>(m));
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
            fireCueInternal(cueName, out, -1);
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
        // VK-1497 — did this step's probability / variant-group roll resolve to "play"?
        bool isPlaying(int stepIndex) const { return plays_[static_cast<size_t>(stepIndex)]; }
        const std::vector<bool>& firedMarkers() const { return firedMarkers_; }

        // Mirrors the Phase-1 completion gate: every step (time- and cue-driven) spawned.
        // VK-1497 — a step resolved not to play never spawns, so treat it as satisfied;
        // otherwise a rolled-out step would stall the combo's completion forever.
        bool allStepsSpawned() const
        {
            for (size_t i = 0; i < spawned_.size(); ++i)
                if (plays_[i] && !spawned_[i])
                    return false;
            return true;
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

        // VK-1497 — the single source of truth for "which steps play this run". A pure
        // function of (data, seed): ungrouped steps roll their independent `probability`;
        // each variantGroup (>=0) yields exactly ONE uniformly-chosen member that always
        // plays (a step's `probability` is reserved as a future per-member selection weight,
        // honored uniformly in v1). Reused by reset()/rewind(), the editor preview, and unit
        // tests so runtime and editor never disagree about which steps are live. Runs only at
        // the reset/rewind boundary — advance() stays a pure reader, so no RNG lives in the
        // per-frame path and seek/prewarm reproduce the same choices.
        static std::vector<bool> resolvePlays(const VFXSequenceData& data, uint32_t effectiveSeed)
        {
            const size_t n = data.steps.size();
            std::vector<bool> plays(n, true);

            // Pass A — independent probability roll, UNGROUPED steps only. Grouped steps are
            // decided entirely in pass B, so they are skipped here.
            for (size_t i = 0; i < n; ++i)
            {
                const VFXSequenceStep& s = data.steps[i];
                if (s.variantGroup >= 0)
                    continue;
                if (s.probability >= 1.0f)
                    continue; // always play (matches VFXBurstTypes short-circuit)
                if (s.probability <= 0.0f)
                {
                    plays[i] = false; // never play
                    continue;
                }
                plays[i] = (probabilityRoll(effectiveSeed, static_cast<int>(i)) < s.probability);
            }

            // Pass B — variant groups: exactly one uniformly-chosen member plays. Each group is
            // processed once (at its smallest-index member) and members are enumerated in
            // ascending step-index order, so the winner is a pure function of (seed, groupId)
            // and never depends on container / iteration order.
            for (size_t i = 0; i < n; ++i)
            {
                const int group = data.steps[i].variantGroup;
                if (group < 0)
                    continue;

                bool firstMember = true;
                for (size_t j = 0; j < i; ++j)
                {
                    if (data.steps[j].variantGroup == group)
                    {
                        firstMember = false;
                        break;
                    }
                }
                if (!firstMember)
                    continue; // this group was already resolved at an earlier member

                uint32_t count = 0;
                for (size_t j = i; j < n; ++j)
                {
                    if (data.steps[j].variantGroup == group)
                        ++count;
                }
                // count >= 1 (step i itself is a member).
                const uint32_t winnerPos = variantWinner(effectiveSeed, group) % count;

                uint32_t pos = 0;
                for (size_t j = i; j < n; ++j)
                {
                    if (data.steps[j].variantGroup != group)
                        continue;
                    plays[j] = (pos == winnerPos);
                    ++pos;
                }
            }

            return plays;
        }

    private:
        // VK-1497 — distinct CPU-only PCG streams so the play roll and the variant-winner pick
        // never correlate with each other, nor with the per-step child seed (deriveSeed) that
        // drives a child instance's visual variance. Kept local (NOT added to VFXVariance.hpp,
        // which is mirrored to vfx_variance.glsl) because these rolls are CPU-only.
        static constexpr uint32_t kProbabilityStream = 0x50524F42u;  // 'PROB'
        static constexpr uint32_t kVariantGroupStream = 0x56475250u; // 'VGRP'

        // Roll in [0,1) for a step's independent probability. Half-open (24-bit mantissa) so
        // P(play) == probability exactly. Derived from the same per-step seed the child uses,
        // but pushed through an extra hash + stream salt to decorrelate it from the child RNG.
        static float probabilityRoll(uint32_t effectiveSeed, int stepIndex)
        {
            const uint32_t h = vfxPcgHash(deriveSeed(effectiveSeed, stepIndex) ^ kProbabilityStream);
            return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); // (h>>8) in [0,2^24) => [0,1)
        }

        // Deterministic 32-bit selector for a variant group, keyed on the STABLE group id (not
        // step index / container order) so replay/seek reproduce the same winner.
        static uint32_t variantWinner(uint32_t effectiveSeed, int groupId)
        {
            return vfxPcgHash(effectiveSeed ^ vfxPcgHash(static_cast<uint32_t>(groupId)) ^
                              kVariantGroupStream);
        }

        void fireCueInternal(std::string_view cueName, std::vector<ComboEvent>& out, int sourceMarker)
        {
            if (cueName.empty())
                return;
            for (size_t i = 0; i < data_->steps.size(); ++i)
            {
                const VFXSequenceStep& s = data_->steps[i];
                if (spawned_[i] || s.cueName.empty() || std::string_view(s.cueName) != cueName ||
                    !plays_[i]) // VK-1497 — a cue-driven step also honors its resolved play/skip
                    continue;
                spawned_[i] = true;
                out.push_back({ComboEventKind::SpawnStep, static_cast<int>(i), sourceMarker});
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
        std::vector<bool> plays_; // VK-1497 — resolved per-step play/skip mask (pure fn of data_+seed_)
    };
}
