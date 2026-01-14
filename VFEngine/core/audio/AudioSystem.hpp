#pragma once
#include <AL/al.h>
#include <AL/alc.h>
#include <string>
#include "../../utilities/types/AudioTypes.hpp"

namespace core::audio
{
    class AudioSystem
    {
    private:
        ALCdevice* device = nullptr;
        ALCcontext* context = nullptr;
        bool initialized = false;
        types::AudioSettings currentSettings = types::AudioSettings::createDefault();

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
