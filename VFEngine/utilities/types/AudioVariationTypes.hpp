#pragma once
#include <cstddef>
#include <cstdint>

// VK-1520: pure audio variation maths — clip selection, the no-immediate-repeat
// rule, and pitch/volume jitter. Header-only inline (no .cpp, no state) so the
// Core play path (core/adapters/api/AudioAPI.cpp), the Editor drawers, and the
// unit tests (tests/) all share one implementation without linking OpenAL or
// Audio.dll — the same arrangement as resource/AudioDownmix.hpp.
//
// Kept in types/ rather than resource/ because AudioPlayOrder is embedded by
// value in AudioSource2D/3DComponent, and components/MediaComponents.hpp
// already includes types/AudioEffectTypes.hpp — so the include direction is
// established and cycle-free. Deliberately NOT folded into the types/
// AudioTypes.hpp hub: that header is embedded by value in SceneGraphSystem, so
// editing it forces a full rebuild for readers that only want this enum.
//
// Every function is a total function of its arguments: no RNG state, no
// globals, no allocation. The caller supplies the random draw (see
// audioVarianceUnit), which is what lets the unit tests assert exact indices
// and sweep the no-repeat rule exhaustively rather than sampling histograms.

namespace types
{
    // How a source picks its clip. Single is the default so every pre-VK-1520
    // scene deserializes into the exact old behaviour: play audioRef, ignore
    // clipVariants.
    enum class AudioPlayOrder : uint8_t
    {
        Single = 0,
        Random = 1,
        RoundRobin = 2
    };

    // Variants are indexed by uint8_t; 0xFF is reserved as a sentinel carrying
    // two meanings the selector treats identically:
    //   as INPUT  (lastVariant): "nothing has played yet on this source"
    //   as RESULT (selectVariant): "the pool is empty — there is nothing to play"
    //
    // Without a distinct sentinel, lastVariant == 0 would mean both "variant 0"
    // and "never played", so no-immediate-repeat would exclude variant 0 from
    // every first play — a 5-variant footstep loop could provably never start on
    // its own first clip. Hence 0xFF, not 0.
    inline constexpr uint8_t AUDIO_VARIANT_NONE = 0xFFu;
    inline constexpr std::size_t AUDIO_MAX_VARIANTS = 255u; // 0..254; 0xFF reserved

    // Jitter safety rails. AudioSource::setPitch/setVolume pass the raw float
    // straight to alSourcef with no clamp of their own, and the two invalid pitches
    // fail differently — which is why the floor is > 0 and not >= 0:
    //   pitch 0 is ACCEPTED (openal-soft al/source.cpp guards `>= T{0}`, not `> 0`).
    //     The mixer then floors the step at 1 instead of 0 (alc/alu.cpp, `std::max(
    //     fastf2u(pitch * MixerFracOne), 1u)`, MixerFracOne == 1<<16), so the voice
    //     advances at 1/65536 speed — a 1s clip holds its pooled slot for ~18h. A
    //     virtualized voice is worse: its simulated clock scales by pitch, so the
    //     clock freezes outright and the voice never expires at all.
    //   pitch < 0 is REJECTED with AL_INVALID_VALUE, which only logs — the source
    //     keeps its PREVIOUS pitch while the mirrored record keeps the negative, and
    //     the two disagree from then on.
    // Jitter reaches 0 on its own (0.5 * (1 + 1.0*-1) == 0), so the clamp is a
    // correctness requirement, not a nicety.
    //
    // The bounds are deliberately wider than the drawers' authored ranges
    // (pitch [0.5, 2.0], volume [0, 1]): authored pitch 2.0 with 50% variation
    // legitimately reaches 3.0. They exist to stop scripts and hand-edited JSON
    // from making an invalid AL call, so they never bite normal authoring.
    inline constexpr float AUDIO_MIN_PITCH = 0.05f; // > 0 per OpenAL; ~20x slowdown
    inline constexpr float AUDIO_MAX_PITCH = 4.0f;  // 2 octaves up
    inline constexpr float AUDIO_MIN_VOLUME = 0.0f; // AL_GAIN >= 0
    inline constexpr float AUDIO_MAX_VOLUME = 1.0f; // the drawers' authored ceiling

    // Decorrelated draws off a single per-play seed. The values need only be
    // pairwise distinct. Keeping clip choice, pitch and volume on separate
    // streams stops them moving in lockstep (the loudest variant always also
    // being the highest-pitched).
    enum class AudioVarianceStream : uint32_t
    {
        ClipSelect = 0x2545F491u,
        Pitch = 0x9E3779B1u,
        Volume = 0x6A09E667u
    };

    // A deliberate COPY of the public-domain PCG round in vfx/VFXVariance.hpp,
    // NOT an include of it. That header is a mirror of resources/shaders/vfx/
    // vfx_variance.glsl — editing it is a GPU contract change. Audio must not be
    // able to break that contract, and a VFX-side shader fix must not silently
    // re-roll every footstep in the game. Four lines buys that independence.
    inline uint32_t audioPcgHash(uint32_t value) noexcept
    {
        uint32_t state = value * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    // Seed for one play of one source.
    //
    // playCount MUST advance on every play. Seeding from lastVariant alone would
    // make (last -> next) a fixed function, collapsing the sequence into a cycle
    // of length <= variantCount — a 5-clip loop would play 0,3,1,4,2,0,3,1,4,2...
    // forever. playCount supplies the entropy; entityId only decorrelates two
    // emitters that fire on the same playCount.
    inline uint32_t audioPlaySeed(uint32_t entityId, uint32_t playCount) noexcept
    {
        return audioPcgHash(entityId * 2654435761u + playCount);
    }

    // Returns [0, 1] — note the CLOSED upper bound. A hash of 0xFFFFFFFF yields
    // exactly 1.0f (both 0xFFFFFFFF and 4294967295.0f round to 2^32 in float32,
    // and 2^32 * 2^-32 == 1.0f), so consumers must clamp their top bucket rather
    // than assume < 1. audioUniformIndex below does exactly that.
    inline float audioVarianceUnit(uint32_t seed, AudioVarianceStream stream) noexcept
    {
        const uint32_t hash = audioPcgHash(seed ^ static_cast<uint32_t>(stream));
        return static_cast<float>(hash) * (1.0f / 4294967295.0f);
    }

    // Returns [-1, 1].
    inline float audioVarianceSigned(uint32_t seed, AudioVarianceStream stream) noexcept
    {
        return audioVarianceUnit(seed, stream) * 2.0f - 1.0f;
    }

    // Maps randomUnit in [0, 1] onto [0, n). Requires n >= 1.
    //
    // The `> 0.0f` test is false for NaN, so a NaN draw yields 0 rather than UB
    // in the float->uint32 conversion. The k >= n branch absorbs randomUnit ==
    // 1.0f (see audioVarianceUnit's closed upper bound).
    inline uint8_t audioUniformIndex(float randomUnit, uint8_t n) noexcept
    {
        if (!(randomUnit > 0.0f))
        {
            return 0u;
        }
        const auto k = static_cast<uint32_t>(randomUnit * static_cast<float>(n));
        return static_cast<uint8_t>(k >= n ? n - 1u : k);
    }

    // Chooses which clip a play should use.
    //
    //   poolCount   : number of playable clips. The caller builds the pool as
    //                 [audioRef] ++ valid(clipVariants), so index 0 is audioRef
    //                 and old scenes (playOrder == Single) always land on it.
    //   lastVariant : the index this source played last; AUDIO_VARIANT_NONE if
    //                 it has never played.
    //   order       : the component's playOrder.
    //   randomUnit  : a fresh draw in [0, 1]; ignored unless order == Random.
    //
    // Returns an index into the pool, or AUDIO_VARIANT_NONE iff the pool is
    // empty (nothing to play — the caller bails).
    //
    // Random guarantees the result != lastVariant whenever poolCount >= 2, by
    // drawing uniformly over the N-1 non-last candidates and REMAPPING past
    // lastVariant — never by rerolling until different, which has no iteration
    // bound and cannot terminate at N == 1.
    inline uint8_t selectVariant(std::size_t poolCount,
                                 uint8_t lastVariant,
                                 AudioPlayOrder order,
                                 float randomUnit) noexcept
    {
        if (poolCount == 0)
        {
            return AUDIO_VARIANT_NONE;
        }
        if (order == AudioPlayOrder::Single)
        {
            return 0u; // pool[0] == audioRef
        }

        const auto n = static_cast<uint8_t>(poolCount < AUDIO_MAX_VARIANTS ? poolCount
                                                                           : AUDIO_MAX_VARIANTS);
        if (n == 1u)
        {
            // No-immediate-repeat is unsatisfiable with one clip. Play it anyway:
            // going silent would be a worse surprise than a repeat.
            return 0u;
        }

        // lastVariant is meaningful only while it still indexes a live clip. One
        // predicate covers three cases: the never-played sentinel (0xFF), a list
        // the user shrank in the drawer, and garbage from a hand-edited scene.
        // It must be tested BEFORE any modulo — (0xFF + 1) % 3 == 1 would make
        // RoundRobin skip variant 0 on its very first play.
        const bool hasLast = (lastVariant < n);

        if (order == AudioPlayOrder::RoundRobin)
        {
            return hasLast ? static_cast<uint8_t>((static_cast<uint32_t>(lastVariant) + 1u) % n)
                           : uint8_t{0};
        }

        // --- Random ---
        if (!hasLast)
        {
            return audioUniformIndex(randomUnit, n); // full N-way uniform
        }

        // Draw k uniformly over the n-1 allowed values, then skip past lastVariant:
        //   k <  lastVariant -> k      (strictly below last, so != last)
        //   k >= lastVariant -> k + 1  (strictly above last, so != last)
        //
        // f(k) = (k >= last ? k+1 : k) is strictly increasing on [0, n-2] hence
        // injective; its image excludes last and is bounded by f(n-2) = n-1 < n.
        // So f maps [0, n-2] onto [0, n) \ {last}, a bijection between sets of
        // equal size n-1 — a uniform k therefore gives a uniform result over
        // exactly the legal candidates, in O(1) with no loop.
        const auto m = static_cast<uint8_t>(n - 1u);
        const uint8_t k = audioUniformIndex(randomUnit, m);
        return static_cast<uint8_t>(k >= lastVariant ? k + 1u : k);
    }

    // Shared jitter core: base * (1 + variation * rSigned), clamped to [lo, hi].
    //
    // Multiplicative, not additive: pitch is a multiplier in OpenAL (AL_PITCH,
    // 1.0 == normal), so "10% variation" stays +/-10% whether the authored pitch
    // is 0.5 or 2.0. Additive would silently halve the effect at pitch 2.0.
    //
    // variation <= 0 returns base UNCLAMPED and bit-exact — that is the
    // back-compat guarantee: every pre-VK-1520 scene (variation defaults to 0)
    // plays byte-identically. The negated form also catches a NaN variation.
    inline float audioApplyVariation(float base, float variation, float rSigned,
                                     float lo, float hi) noexcept
    {
        if (!(variation > 0.0f))
        {
            return base;
        }
        const float jittered = base * (1.0f + variation * rSigned);
        if (!(jittered > lo))
        {
            return lo; // NaN-safe
        }
        return jittered < hi ? jittered : hi;
    }

    inline float audioJitterPitch(float pitch, float variation, float rSigned) noexcept
    {
        return audioApplyVariation(pitch, variation, rSigned, AUDIO_MIN_PITCH, AUDIO_MAX_PITCH);
    }

    // The same rails for a pitch that arrives already authored — from a script native or
    // hand-edited JSON — rather than from the jitter path above. Unlike
    // audioApplyVariation there is no unclamped passthrough: an authored pitch has no
    // "variation == 0 means don't touch it" contract to honour.
    //
    // Every writer of a voice's pitch must route through this. The AL source and the
    // CPU-side record that mirrors it are written from the same command, so clamping one
    // and not the other is what makes them diverge.
    inline constexpr float audioClampPitch(float pitch) noexcept
    {
        if (!(pitch > AUDIO_MIN_PITCH))
        {
            return AUDIO_MIN_PITCH; // NaN-safe
        }
        return pitch < AUDIO_MAX_PITCH ? pitch : AUDIO_MAX_PITCH;
    }

    // Note the asymmetry at the top of the range: the drawers author volume in
    // [0, 1], so a source at volume 1.0 with variation only ever ducks (1.0 +/-
    // 20% clamps to [0.8, 1.0]). That is intentional and matches Unity, whose
    // AudioSource volume ceiling is likewise 1.0 — a designer wanting symmetric
    // swing authors ~0.9 for headroom. The drawer tooltip says so.
    inline float audioJitterVolume(float volume, float variation, float rSigned) noexcept
    {
        return audioApplyVariation(volume, variation, rSigned, AUDIO_MIN_VOLUME, AUDIO_MAX_VOLUME);
    }
}
