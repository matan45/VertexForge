#pragma once
#include <AL/al.h>
#include <AL/alc.h>
#include <string>

namespace core::audio {

    enum class DistanceModel : uint8_t {
        InverseDistance,
        InverseDistanceClamped,
        LinearDistance,
        LinearDistanceClamped,
        ExponentDistance,
        ExponentDistanceClamped,
        None
    };

    class AudioSystem {
    public:
        AudioSystem() = default;
        ~AudioSystem();

        AudioSystem(const AudioSystem&) = delete;
        AudioSystem& operator=(const AudioSystem&) = delete;
        AudioSystem(AudioSystem&&) = delete;
        AudioSystem& operator=(AudioSystem&&) = delete;

        bool init();
        void cleanUp();
        bool isInitialized() const { return initialized; }

        void setDistanceModel(DistanceModel model);
        DistanceModel getDistanceModel() const { return currentDistanceModel; }

        void setDopplerFactor(float factor);
        float getDopplerFactor() const { return dopplerFactor; }

        void setSpeedOfSound(float speed);
        float getSpeedOfSound() const { return speedOfSound; }

        std::string getDeviceName() const;
        std::string getVendor() const;
        std::string getVersion() const;
        std::string getRenderer() const;

        static bool checkError(const char* operation);

    private:
        ALCdevice* device = nullptr;
        ALCcontext* context = nullptr;
        bool initialized = false;
        DistanceModel currentDistanceModel = DistanceModel::InverseDistanceClamped;
        float dopplerFactor = 1.0f;
        float speedOfSound = 343.3f;
    };

}
