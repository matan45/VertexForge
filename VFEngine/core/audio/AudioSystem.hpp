#pragma once

#include <AL/alc.h>
#include <AL/al.h>
#include <AL/efx.h>
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
        types::AudioSettings currentSettings = types::AudioSettings::createDefault();

        static bool s_efxAvailable;

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

        std::string getDeviceName() const;
        std::string getVendor() const;
        std::string getVersion() const;
        std::string getRenderer() const;

        bool isEfxSupported() const { return efxSupported; }
        static bool isEfxAvailable() { return s_efxAvailable; }

        // Global audio settings
        void setMasterVolume(float volume);
        void setDopplerFactor(float factor);
        void setSpeedOfSound(float speed);
        void setDistanceModel(types::AudioDistanceModel model);
        void applySettings(const types::AudioSettings& settings);
        types::AudioSettings getCurrentSettings() const { return currentSettings; }

        static bool checkError(const char* operation);
    };
}
