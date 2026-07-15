#pragma once
#include "AudioSource.hpp"
#include "FadePolicy.hpp"  // VK-1521: the one expression of the ramp curve
#include "VoicePolicy.hpp" // kDefaultMaxRealVoices — the pool's ceiling is the voice budget
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <memory>

namespace core::audio
{
    using AudioHandle = uint64_t;
    constexpr AudioHandle InvalidAudioHandle = 0;

    class AudioSourceManager
    {
    private:
        std::vector<std::unique_ptr<AudioSource>> sourcePool;
        std::vector<size_t> freeIndices;
        std::unordered_map<AudioHandle, size_t> activeHandles;

        uint64_t nextHandleId = 1;

        // VK-1513: ceiling on how far the pool may grow. <= 0 means unbounded (cap off).
        // Overwritten by ApplySettingsCmd from the scene's AudioSettings::maxRealVoices;
        // this is only the pre-settings default. The pool still grows lazily rather than
        // being pre-sized, because initPool() runs during AudioController::init() while the
        // settings only arrive later — the cap is simply not known yet at init time.
        int maxVoices = kDefaultMaxRealVoices;

    public:
        explicit AudioSourceManager(size_t initialPoolSize = 32);
        ~AudioSourceManager();

        AudioSourceManager(const AudioSourceManager&) = delete;
        AudioSourceManager& operator=(const AudioSourceManager&) = delete;

        AudioHandle acquireSource();
        void releaseSource(AudioHandle handle);

        AudioSource* getSource(AudioHandle handle);
        const AudioSource* getSource(AudioHandle handle) const;
        void setVolume(AudioHandle handle, float volume);

        void stopAll();
        void update();
        void updateFilters(const glm::vec3& listenerPos, float deltaTime);

        // VK-1521. Both arm an entry in fadingQueue; updateFades advances them.
        //
        // THE MEMBERSHIP INVARIANT, which every mutator below must preserve:
        //  - fade-OUT: handle is in fadingQueue and NOT in activeHandles. The voice is
        //    dying, so update() must not auto-release it out from under the ramp.
        //  - fade-IN:  handle is in fadingQueue AND STILL in activeHandles. It is an
        //    ordinary playing voice that merely happens to be ramping, so update() must
        //    still reap it when its clip ends and updateFilters must still run its
        //    distance filter. This dual membership is why releaseSource has to clear
        //    fadingQueue on BOTH of its branches, not just the fading one.
        //  - At most ONE fadingQueue entry per handle, ever. The queue is scanned with
        //    find_if, so a second entry would shadow the first and leave updateFades
        //    advancing one ramp while setVolume/getFadeGain reported the other.
        void startFadeIn(AudioHandle handle, float durationMs);
        void startFadeOut(AudioHandle handle, float durationMs);
        void updateFades(float deltaTimeMs);
        float getFadeGain(AudioHandle handle) const;

        size_t getActiveCount() const { return activeHandles.size(); }
        size_t getPoolSize() const { return sourcePool.size(); }

        // VK-1513. The pool is never shrunk — destroying AL sources under live voices would
        // cut them off mid-playback. Lowering the cap only stops further growth.
        void setMaxVoices(int cap) { maxVoices = cap; }
        int getMaxVoices() const { return maxVoices; }

        void initPool(size_t poolSize);

    private:
        struct FadingSource
        {
            AudioHandle handle;
            size_t poolIndex;
            // The bus-multiplied TARGET (userVolume * effectiveBusVolume), NOT the current
            // output: AL_GAIN carries baseVolume scaled by the ramp. setVolume is its sole
            // authority and rewrites it on every bus flush, which is what stops the per-tick
            // flush from stomping the fade. Never re-read it back out of AL_GAIN.
            float baseVolume;
            // Where the ramp starts, as a multiplier. 1.0 for an ordinary fade; only differs
            // when a fade-out takes over from a fade-in mid-flight. Kept separate from
            // baseVolume on purpose — see fade::rampGain.
            float startGain;
            float remainingMs;
            float totalMs;
            // VK-1521. Tail-appended with a default so the pre-existing brace-init
            // {handle, index, currentVolume, durationMs, durationMs} still compiles and
            // still means what it always did.
            fade::FadeDirection direction = fade::FadeDirection::Out;
        };
        std::vector<FadingSource> fadingQueue;

        // Shared by every fadingQueue lookup below — the queue is a linear scan by design
        // (it holds a handful of entries at most), and centralising it keeps the
        // one-entry-per-handle invariant in a single place.
        std::vector<FadingSource>::iterator findFade(AudioHandle handle);
        std::vector<FadingSource>::const_iterator findFade(AudioHandle handle) const;
        // Stop a source, detach its filter, drop its buffer and hand the pool slot back.
        void retireSlot(size_t poolIndex);

        void growPool(size_t additionalSources);
        AudioHandle generateHandle();
    };
}
