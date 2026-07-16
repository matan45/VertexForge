#pragma once

#include <AL/alc.h>
#include <AL/al.h>
#include <AL/efx.h>
#include <AL/alext.h>
#include <atomic>
#include <string>
#include "types/AudioTypes.hpp"

namespace core::audio
{
    class AudioSystem
    {
    private:
        ALCdevice* device = nullptr;
        ALCcontext* context = nullptr;
        bool initialized = false;
        bool efxSupported = false;
        bool spatializeSupported = false;
        types::AudioSettings currentSettings = types::AudioSettings::createDefault();

        static bool s_efxAvailable;
        static bool s_spatializeAvailable;

        // VK-1508: HRTF (ALC_SOFT_HRTF). Device-scope extension detected + toggled only
        // inside AudioSystem, so (unlike EFX) no static cross-TU accessor is needed.
        bool hrtfSupported = false;
        bool hrtfApplied = false;                          // first applySettings forces the toggle authoritative
        std::atomic<int> hrtfStatus{ -1 };                 // ALC_HRTF_STATUS_SOFT; -1 == unsupported. Written on audio thread, read on main.
        LPALCRESETDEVICESOFT alcResetDeviceSOFT = nullptr; // loaded via alcGetProcAddress (not directly linked)

    public:
        // EFX filter function pointers (loaded dynamically)
        static LPALGENFILTERS alGenFilters;
        static LPALDELETEFILTERS alDeleteFilters;
        static LPALFILTERI alFilteri;
        static LPALFILTERF alFilterf;

        // EFX effect function pointers
        static LPALGENEFFECTS alGenEffects;
        static LPALDELETEEFFECTS alDeleteEffects;
        static LPALEFFECTI alEffecti;
        static LPALEFFECTF alEffectf;
        static LPALEFFECTFV alEffectfv;
        static LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
        static LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
        static LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
        static LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;

        int getMaxAuxiliarySends() const { return maxAuxiliarySends; }

    private:
        int maxAuxiliarySends = 0;

    public:
        explicit AudioSystem() = default;
        ~AudioSystem();

        AudioSystem(const AudioSystem&) = delete;
        AudioSystem& operator=(const AudioSystem&) = delete;
        AudioSystem(AudioSystem&&) = delete;
        AudioSystem& operator=(AudioSystem&&) = delete;

        bool init();
        void cleanUp();
        bool isInitialized() const { return initialized; }

        // Thread context transfer: release on main thread, acquire on audio thread
        void releaseContext();
        void acquireContext();

        std::string getDeviceName() const;
        std::string getVendor() const;
        std::string getVersion() const;
        std::string getRenderer() const;

        bool isEfxSupported() const { return efxSupported; }
        static bool isEfxAvailable() { return s_efxAvailable; }

        // AL_SOFT_source_spatialize: lets OpenAL-soft spatialize non-mono buffers
        // when forced on 3D sources. Static getter because AudioSource reaches
        // AudioSystem only through statics (no instance pointer).
        bool isSpatializeSupported() const { return spatializeSupported; }
        static bool isSpatializeAvailable() { return s_spatializeAvailable; }

        // Global audio settings
        void setMasterVolume(float volume);
        void setDopplerFactor(float factor);
        void setSpeedOfSound(float speed);
        void setDistanceModel(types::AudioDistanceModel model);
        void applySettings(const types::AudioSettings& settings);
        types::AudioSettings getCurrentSettings() const { return currentSettings; }

        // VK-1508: live device HRTF status (audio thread writes via applySettings/init, main thread reads).
        types::AudioHrtfStatus getHrtfStatus() const
        {
            return static_cast<types::AudioHrtfStatus>(hrtfStatus.load(std::memory_order_relaxed));
        }

        static bool checkError(const char* operation);
    };
}
