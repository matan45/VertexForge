#pragma once

// VK-1513 — audio voice limiting + priority.
//
// Pure, header-only decision core for bounding the number of simultaneously playing
// (real) voices by importance. When a play request arrives and the scene-wide budget
// is already full, the policy either denies it, or steals the slot of the single
// least-important live voice. Each voice is ranked by its estimated audible gain
// (distance-attenuated, post-bus) scaled by an authored priority weight.
//
// AL-free / device-free (like utilities/vfx/VFXSignificance.hpp, which solves the same
// top-N-by-importance shape for VFX, and core/physics/SpatialQueryHelpers.hpp) so the
// CPU-only Tests project can unit-test the attenuation model, the ranking and the
// decision directly. Tests links neither Audio nor OpenAL and has no openal-soft
// includedir, so everything here MUST stay inline, header-only, AL-include-free, and
// must NOT be marked VF_AUDIO_API (that macro resolves to dllimport outside the DLL,
// which would leave Tests with unresolved externals).
//
// VK-1515 landed that follow-up: virtualization (a losing voice kept alive on a simulated
// playback clock and revived when it becomes audible again). It arrived as rebalanceVoices()
// below plus a virtual-voice registry on AudioThread, and deliberately NOT as a Virtualize
// enumerator on VoiceDecisionKind: decidePlay answers a play-time question ("does this
// request get a real slot?") whose Allow/Deny/Steal answers are unchanged and still pinned
// by test_audio_voice_policy.cpp. Whether a denied or stolen voice is discarded or kept on a
// clock is the caller's policy, and promotion/demotion is a per-TICK question that needs its
// own entry point regardless. Ranking every tick DOES introduce real<->virtual flapping, so
// unlike decidePlay, rebalanceVoices needs hysteresis.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "types/AudioTypes.hpp"

namespace core::audio
{
    // Mirrors InvalidAudioHandle in AudioSourceManager.hpp, restated here because that
    // header reaches OpenAL transitively and this one must not.
    inline constexpr uint64_t kInvalidVoiceHandle = 0;

    // Neutral authored priority.
    //
    // LOWER = MORE IMPORTANT: 0 = critical, 255 = least important, 128 = neutral.
    // Matches Unity/FMOD (so authored content ports without inversion) and
    // components::VFXComponent::priority (uint8_t, 0=Critical..3=Low), which is the
    // closest analogue in this engine — also a keep-or-drop budget decision.
    // NOTE components::ReverbZoneComponent::priority uses the OPPOSITE convention
    // (higher wins); that is overlapping-zone override, a different problem.
    //
    // 128 is neutral in the strong sense: priorityWeight(128) == 1.0 exactly, so
    // voiceScore() of an un-authored voice IS its audible gain, and every call site
    // that never sets a priority keeps today's relative ordering.
    inline constexpr uint8_t kDefaultVoicePriority = 128;

    // Default scene-wide real-voice budget. 2x the initial pool (AudioController's
    // initPool(32)), so the pool's first growth step is also its ceiling and no
    // existing content changes behaviour.
    inline constexpr int kDefaultMaxRealVoices = 64;

    // VK-1515. Ceiling on voices kept alive on a simulated clock after losing the real
    // budget. Matches Unity's 512 virtual voices. A virtual voice costs a record and a
    // float add per tick — no AL source, no decode — so this is a runaway guard, not a
    // budget in the kDefaultMaxRealVoices sense: content that legitimately wants 512
    // simultaneous sounds is already pathological. Beyond it, a losing voice is dropped
    // outright, which is exactly VK-1513's pre-virtualization behaviour.
    inline constexpr int kDefaultMaxVirtualVoices = 512;

    // VK-1515. How decisively a virtual voice must out-score the worst real voice before
    // it takes that slot: 1.25 = a 25% louder-after-priority win, ~1.9 dB.
    //
    // This is the anti-flap term and the reason rebalanceVoices exists as its own function.
    // Two voices within a hair of each other at the budget edge would otherwise swap every
    // tick, and a swap is not free: it is an AL source release plus an acquire + seek +
    // play, 200x/second, audible as chatter. Requiring a margin means the incumbent keeps
    // its slot through noise and only yields to a real change in the scene.
    //
    // Deliberately a ratio, not a difference: scores are gains spanning ~96 dB (see
    // kPriorityStepsPerOctave), so a fixed epsilon would be meaningless at the top of the
    // range and infinitely sticky at the bottom.
    inline constexpr float kVoiceHysteresisRatio = 1.25f;

    // Priority authority: one octave (6.02 dB) of gain per this many priority steps.
    //
    // Sized deliberately. Across the full 0..255 range the weight spans 2^15.94 ~= 62580x
    // ~= 95.9 dB — about the dynamic range of 16-bit audio. So priority can override any
    // AUDIBLE gain difference, while a priority-0 sound that is genuinely below the noise
    // floor still loses to one you can actually hear. Do not round this to 32: at 48 dB
    // of authority a close explosion out-ranks (and steals) a quiet music bed.
    inline constexpr float kPriorityStepsPerOctave = 16.0f;

    enum class VoiceDecisionKind : uint8_t
    {
        Allow, // start normally; a free slot exists (or the cap is disabled)
        Deny,  // budget full and this request loses; never allocate a source
        Steal  // budget full and this request wins; release `victim` first
    };

    // Distance-attenuation inputs, one per voice. Field-for-field the AL source
    // properties: AL_REFERENCE_DISTANCE / AL_MAX_DISTANCE / AL_ROLLOFF_FACTOR, plus the
    // context-wide distance model.
    struct AttenuationParams
    {
        types::AudioDistanceModel model = types::AudioDistanceModel::InverseDistanceClamped;
        float refDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloffFactor = 1.0f;
    };

    // One voice considered by the budget. `handle` is the caller's stable voice id, used
    // as the final tie-break so ranking never depends on unordered_map iteration order.
    // `audibleGain` is the post-distance, post-bus estimate from estimateAudibleGain().
    struct VoiceCandidate
    {
        uint64_t handle = kInvalidVoiceHandle;
        uint8_t priority = kDefaultVoicePriority;
        float audibleGain = 1.0f;
    };

    struct VoiceDecision
    {
        VoiceDecisionKind kind = VoiceDecisionKind::Allow;
        uint64_t victim = kInvalidVoiceHandle; // meaningful iff kind == Steal
    };

    // --- Attenuation model -------------------------------------------------------
    //
    // Mirrors dependencies/openal-soft/alc/alu.cpp (CalcAttnSourceParams, ~:1763-1846)
    // rather than the OpenAL 1.1 spec prose, because openal-soft is what actually runs
    // and it diverges from a naive reading in four places (each marked below). The
    // estimate must agree with what the listener hears, or the ranking lies.
    //
    // If dependencies/openal-soft is ever bumped, re-diff these two functions against
    // CalcAttnSourceParams; test_audio_voice_policy.cpp pins every divergence.

    // alu.cpp:1763-1781. Clamped models pin the distance into [ref, max] first.
    inline float attenuationDistance(const AttenuationParams& p, float distance)
    {
        switch (p.model)
        {
        case types::AudioDistanceModel::InverseDistanceClamped:
        case types::AudioDistanceModel::LinearDistanceClamped:
        case types::AudioDistanceModel::ExponentDistanceClamped:
            // (1) alu.cpp:1770. An inverted range disables attenuation outright — a
            // source 1000 m away plays at FULL volume. Spelled as !(ref <= max), not
            // (ref > max), so a NaN bound also lands here instead of reaching clamp().
            if (!(p.refDistance <= p.maxDistance))
                return p.refDistance;
            return std::clamp(distance, p.refDistance, p.maxDistance);

        case types::AudioDistanceModel::None:
        case types::AudioDistanceModel::InverseDistance:
        case types::AudioDistanceModel::LinearDistance:
        case types::AudioDistanceModel::ExponentDistance:
            break;
        }
        return distance;
    }

    // alu.cpp:1788-1846. Every guarded-out branch returns 1.0 (no attenuation),
    // mirroring alu.cpp's `dryAttnBase = 1.0f` default.
    inline float distanceAttenuation(const AttenuationParams& p, float distance)
    {
        const float d = attenuationDistance(p, distance);

        switch (p.model)
        {
        case types::AudioDistanceModel::InverseDistance:
        case types::AudioDistanceModel::InverseDistanceClamped:
            // alu.cpp:1791-1812. (2) refDistance <= 0 disables the model.
            if (p.refDistance > 0.0f)
            {
                // lerpf(ref, d, rolloff) expanded (alnumeric.h:114-116).
                const float dist = p.refDistance + (d - p.refDistance) * p.rolloffFactor;
                if (dist > 0.0f)
                    return p.refDistance / dist;
            }
            return 1.0f;

        case types::AudioDistanceModel::LinearDistance:
        case types::AudioDistanceModel::LinearDistanceClamped:
            // alu.cpp:1814-1828. (3) max == ref disables the model.
            if (p.maxDistance != p.refDistance)
            {
                const float scale = (d - p.refDistance) / (p.maxDistance - p.refDistance);
                // (4) Unclamped and closer than refDistance, scale goes negative and this
                // exceeds 1.0. That is real AL behaviour; estimateAudibleGain() clamps it.
                return std::max(1.0f - scale * p.rolloffFactor, 0.0f);
            }
            return 1.0f;

        case types::AudioDistanceModel::ExponentDistance:
        case types::AudioDistanceModel::ExponentDistanceClamped:
            // alu.cpp:1830-1842.
            if (d > 0.0f && p.refDistance > 0.0f)
                return std::pow(d / p.refDistance, -p.rolloffFactor);
            return 1.0f;

        case types::AudioDistanceModel::None:
            break;
        }
        return 1.0f;
    }

    // Estimated gain the listener actually hears from a voice `distance` away.
    //
    // `sourceGain` is the voice's AL_GAIN, which in this engine already carries
    // userVolume * effectiveBusVolume (AudioBusManagerEffects applies it per source), so
    // bus volume, mute and solo are folded in for free — mute a bus and its voices become
    // stealable, with no duplicated bus math and no extra locking.
    //
    // Pass distance = 0 for a 2D voice: those are AL_SOURCE_RELATIVE at the origin, so a
    // listener-to-source distance is meaningless for them and they attenuate not at all.
    //
    // Cone attenuation is deliberately excluded. Cone gain can only ever REDUCE, so
    // omitting it makes this an upper bound — conservative in the safe direction: the
    // policy may keep a voice that is quieter than it thinks, but it will never deny or
    // steal one that is actually audible.
    inline float estimateAudibleGain(float sourceGain, const AttenuationParams& p, float distance)
    {
        // DELIBERATE DIVERGENCE from alu.cpp, and the one place this model must not mirror
        // it. Feed openal-soft a NaN distance and every `if (dist > 0.0f)` guard above is
        // false, so it falls through to dryAttnBase = 1.0 — i.e. a garbage position yields
        // FULL gain. For mixing that is a harmless shrug; for ranking it is the worst
        // possible answer, because the broken voice would score as the most audible thing
        // in the scene and become un-stealable. Score it silent instead, so it is evicted
        // first — the same call that VFXSignificance::significanceScore makes.
        // (Authored params are not re-checked here: they arrive from a clamped editor field
        // or a struct default, whereas distance is derived from a live transform.)
        if (!std::isfinite(sourceGain) || !std::isfinite(distance))
            return 0.0f;

        // alu.cpp:1880-1883 clamps to [MinGain, MaxGain]. VertexForge never calls
        // alSourcef(AL_MIN_GAIN/AL_MAX_GAIN), so the AL defaults (0 / 1) hold.
        //
        // The listener/master gain applied right after (alu.cpp:1884) is deliberately
        // NOT included: it is a global scalar, so it cannot change relative ranking, and
        // folding it in would shrink the voice budget as the player lowers master volume.
        const float gain = sourceGain * distanceAttenuation(p, distance);
        if (!std::isfinite(gain))
            return 0.0f;
        return std::clamp(gain, 0.0f, 1.0f);
    }

    // --- Ranking -----------------------------------------------------------------

    // Authored importance as a gain multiplier. Lower priority => larger weight.
    // Always finite and > 0: the exponent is bounded to [-7.9375, 8].
    inline float priorityWeight(uint8_t priority)
    {
        return std::exp2((static_cast<float>(kDefaultVoicePriority) - static_cast<float>(priority))
                         / kPriorityStepsPerOctave);
    }

    // Importance of a live voice: higher = keep. Always finite — a non-finite audible
    // gain (a NaN transform feeding the distance, say) sanitizes to 0, both because a NaN
    // score would make moreImportant() violate strict-weak-ordering (UB in std::sort and
    // friends) and because "least important" is the safe outcome for a voice whose
    // position is already garbage.
    inline float voiceScore(const VoiceCandidate& v)
    {
        if (!std::isfinite(v.audibleGain))
            return 0.0f;
        return v.audibleGain * priorityWeight(v.priority);
    }

    // Strict-weak "more important than": by score descending, then by handle ascending
    // (the older / lower-handled voice survives a tie). Handles are unique, so this is a
    // TOTAL order and the chosen victim is fully deterministic regardless of the caller's
    // gather order.
    //
    // Deliberately NOT lexicographic on priority (Unity's rule). Under strict priority a
    // horde of distant, inaudible, marginally-better-prioritized sounds starves every
    // nearby one — a priority-100 sound 300 m out at gain 0.0001 would beat a priority-101
    // sound at 1 m and full volume. Blending instead (see kPriorityStepsPerOctave) keeps
    // priority decisive across the audible range while letting audibility break the tie
    // beyond it. test_audio_voice_policy.cpp pins both halves of that trade.
    inline bool moreImportant(const VoiceCandidate& a, const VoiceCandidate& b)
    {
        const float sa = voiceScore(a);
        const float sb = voiceScore(b);
        if (sa != sb)
            return sa > sb;
        return a.handle < b.handle;
    }

    // Decide what to do with an incoming play request against the live voice set.
    //
    // `maxRealVoices` <= 0 disables the cap entirely (opt-in, matching the VK-1503
    // significance-cap precedent). Streaming voices must not be passed in: they never draw
    // from the pooled sources this budget governs, and being AL_SOURCE_RELATIVE they have
    // no world position to score.
    inline VoiceDecision decidePlay(std::span<const VoiceCandidate> live,
                                    const VoiceCandidate& incoming,
                                    int maxRealVoices)
    {
        if (maxRealVoices <= 0 || live.empty() || static_cast<int>(live.size()) < maxRealVoices)
            return {VoiceDecisionKind::Allow, kInvalidVoiceHandle};

        const VoiceCandidate* worst = &live.front();
        for (const VoiceCandidate& v : live.subspan(1))
        {
            if (moreImportant(*worst, v))
                worst = &v;
        }

        if (moreImportant(incoming, *worst))
            return {VoiceDecisionKind::Steal, worst->handle};

        return {VoiceDecisionKind::Deny, kInvalidVoiceHandle};
    }

    // --- Virtualization (VK-1515) -------------------------------------------------

    // Which voices swap state this tick. Handles only: moving a voice between real and
    // virtual is an AL + registry operation, which this header must not know about.
    struct VoiceRebalance
    {
        std::vector<uint64_t> demote;  // real -> virtual: release the AL source, keep the clock
        std::vector<uint64_t> promote; // virtual -> real: revive at the clocked offset
    };

    // The per-tick half of the budget: decidePlay decides who gets a slot at play time,
    // this decides who KEEPS one as the scene moves. Both sequences are ranked by the same
    // voiceScore, so a virtual voice's gain estimate must be built the same way as a real
    // one's — including bus volume/mute/solo, which a real voice carries implicitly in its
    // AL_GAIN (see gatherLiveVoices). Score a virtual voice on its raw authored volume and
    // it will out-rank real voices on a muted bus and evict audible sound.
    //
    // Callers should rate-limit this rather than run it at the full tick rate: it sorts
    // both sets, and nothing it reacts to (listener/emitter motion) moves at 200Hz.
    inline VoiceRebalance rebalanceVoices(std::span<const VoiceCandidate> real,
                                          std::span<const VoiceCandidate> virt,
                                          int maxRealVoices,
                                          float hysteresisRatio = kVoiceHysteresisRatio)
    {
        VoiceRebalance out;

        // Cap disabled: nothing is virtual by rights, so revive everything and demote nobody.
        if (maxRealVoices <= 0)
        {
            out.promote.reserve(virt.size());
            for (const VoiceCandidate& v : virt)
                out.promote.push_back(v.handle);
            return out;
        }

        // The overwhelmingly common case — nothing virtualized and room to spare — costs two
        // integer compares and allocates nothing.
        if (virt.empty() && real.size() <= static_cast<std::size_t>(maxRealVoices))
            return out;

        // Worst real first, best virtual first. moreImportant is a TOTAL order (score, then
        // handle), so both orderings are fully determined regardless of the caller's gather
        // order — which is unordered_map iteration and therefore arbitrary.
        std::vector<const VoiceCandidate*> reals;
        reals.reserve(real.size());
        for (const VoiceCandidate& v : real)
            reals.push_back(&v);
        std::sort(reals.begin(), reals.end(),
                  [](const VoiceCandidate* a, const VoiceCandidate* b) { return moreImportant(*b, *a); });

        std::vector<const VoiceCandidate*> virts;
        virts.reserve(virt.size());
        for (const VoiceCandidate& v : virt)
            virts.push_back(&v);
        std::sort(virts.begin(), virts.end(),
                  [](const VoiceCandidate* a, const VoiceCandidate* b) { return moreImportant(*a, *b); });

        std::size_t realIdx = 0; // next-worst real, the eviction candidate
        std::size_t virtIdx = 0; // next-best virtual, the promotion candidate

        // Over budget. Reachable in normal use: ApplySettingsCmd can lower maxRealVoices
        // under live voices, and the pool is never shrunk (AudioSourceManager:55-57).
        while (reals.size() - realIdx > static_cast<std::size_t>(maxRealVoices))
            out.demote.push_back(reals[realIdx++]->handle);

        // A free slot displaces nobody, so it needs no margin — hysteresis exists to protect
        // an incumbent, and here there is none.
        std::size_t liveCount = reals.size() - realIdx;
        while (virtIdx < virts.size() && liveCount < static_cast<std::size_t>(maxRealVoices))
        {
            out.promote.push_back(virts[virtIdx++]->handle);
            ++liveCount;
        }

        // Contested: taking an occupied slot demands a decisive win. Spelled !(a > b) rather
        // than (a <= b) so a NaN ratio stops the loop instead of swapping everything.
        // Voices promoted into free slots above are not in `reals` and so cannot be demoted
        // in the same pass — no voice flips twice per tick.
        while (virtIdx < virts.size() && realIdx < reals.size())
        {
            const float incoming = voiceScore(*virts[virtIdx]);
            const float worst = voiceScore(*reals[realIdx]);
            if (!(incoming > worst * hysteresisRatio))
                break; // sorted best-first: if this one can't win, none behind it can

            out.demote.push_back(reals[realIdx++]->handle);
            out.promote.push_back(virts[virtIdx++]->handle);
        }

        return out;
    }
}
