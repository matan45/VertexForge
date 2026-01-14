#include "AudioSystem.hpp"
#include "print/Logger.hpp"

namespace core::audio {

    AudioSystem::~AudioSystem() {
        if (initialized) {
            cleanUp();
        }
    }

    bool AudioSystem::init() {
        if (initialized) {
            loggerWarning("AudioSystem already initialized");
            return true;
        }

        device = alcOpenDevice(nullptr);
        if (!device) {
            loggerError("Failed to open default OpenAL device");
            return false;
        }

        context = alcCreateContext(device, nullptr);
        if (!context) {
            loggerError("Failed to create OpenAL context");
            alcCloseDevice(device);
            device = nullptr;
            return false;
        }

        if (!alcMakeContextCurrent(context)) {
            loggerError("Failed to make OpenAL context current");
            alcDestroyContext(context);
            alcCloseDevice(device);
            context = nullptr;
            device = nullptr;
            return false;
        }

        alGetError();

        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);

        initialized = true;

        loggerInfo("AudioSystem initialized successfully");
        loggerInfo("  Device: {}", getDeviceName());
        loggerInfo("  Vendor: {}", getVendor());
        loggerInfo("  Version: {}", getVersion());
        loggerInfo("  Renderer: {}", getRenderer());

        return true;
    }

    void AudioSystem::cleanUp() {
        if (!initialized) {
            return;
        }

        alcMakeContextCurrent(nullptr);

        if (context) {
            alcDestroyContext(context);
            context = nullptr;
        }

        if (device) {
            alcCloseDevice(device);
            device = nullptr;
        }

        initialized = false;
        loggerInfo("AudioSystem cleaned up");
    }

    std::string AudioSystem::getDeviceName() const {
        if (!device) return "N/A";
        const ALCchar* name = alcGetString(device, ALC_DEVICE_SPECIFIER);
        return name ? std::string(name) : "Unknown";
    }

    std::string AudioSystem::getVendor() const {
        if (!initialized) return "N/A";
        const ALchar* vendor = alGetString(AL_VENDOR);
        return vendor ? std::string(vendor) : "Unknown";
    }

    std::string AudioSystem::getVersion() const {
        if (!initialized) return "N/A";
        const ALchar* version = alGetString(AL_VERSION);
        return version ? std::string(version) : "Unknown";
    }

    std::string AudioSystem::getRenderer() const {
        if (!initialized) return "N/A";
        const ALchar* renderer = alGetString(AL_RENDERER);
        return renderer ? std::string(renderer) : "Unknown";
    }

    bool AudioSystem::checkError(const char* operation) {
        ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            const char* errorStr;
            switch (error) {
                case AL_INVALID_NAME:
                    errorStr = "AL_INVALID_NAME";
                    break;
                case AL_INVALID_ENUM:
                    errorStr = "AL_INVALID_ENUM";
                    break;
                case AL_INVALID_VALUE:
                    errorStr = "AL_INVALID_VALUE";
                    break;
                case AL_INVALID_OPERATION:
                    errorStr = "AL_INVALID_OPERATION";
                    break;
                case AL_OUT_OF_MEMORY:
                    errorStr = "AL_OUT_OF_MEMORY";
                    break;
                default:
                    errorStr = "Unknown error";
                    break;
            }
            loggerError("OpenAL error in {}: {} (0x{:X})", operation, errorStr, error);
            return true;
        }
        return false;
    }

    void AudioSystem::setMasterVolume(float volume) {
        if (!initialized) {
            loggerWarning("AudioSystem::setMasterVolume called but system not initialized");
            return;
        }
        alListenerf(AL_GAIN, volume);
        checkError("setMasterVolume");
        currentSettings.masterVolume = volume;
    }

    void AudioSystem::setDopplerFactor(float factor) {
        if (!initialized) {
            loggerWarning("AudioSystem::setDopplerFactor called but system not initialized");
            return;
        }
        alDopplerFactor(factor);
        checkError("setDopplerFactor");
        currentSettings.dopplerFactor = factor;
    }

    void AudioSystem::setSpeedOfSound(float speed) {
        if (!initialized) {
            loggerWarning("AudioSystem::setSpeedOfSound called but system not initialized");
            return;
        }
        alSpeedOfSound(speed);
        checkError("setSpeedOfSound");
        currentSettings.speedOfSound = speed;
    }

    void AudioSystem::setDistanceModel(types::AudioDistanceModel model) {
        if (!initialized) {
            loggerWarning("AudioSystem::setDistanceModel called but system not initialized");
            return;
        }
        ALenum alModel;
        switch (model) {
            case types::AudioDistanceModel::None:
                alModel = AL_NONE;
                break;
            case types::AudioDistanceModel::InverseDistance:
                alModel = AL_INVERSE_DISTANCE;
                break;
            case types::AudioDistanceModel::InverseDistanceClamped:
                alModel = AL_INVERSE_DISTANCE_CLAMPED;
                break;
            case types::AudioDistanceModel::LinearDistance:
                alModel = AL_LINEAR_DISTANCE;
                break;
            case types::AudioDistanceModel::LinearDistanceClamped:
                alModel = AL_LINEAR_DISTANCE_CLAMPED;
                break;
            case types::AudioDistanceModel::ExponentDistance:
                alModel = AL_EXPONENT_DISTANCE;
                break;
            case types::AudioDistanceModel::ExponentDistanceClamped:
                alModel = AL_EXPONENT_DISTANCE_CLAMPED;
                break;
            default:
                alModel = AL_INVERSE_DISTANCE_CLAMPED;
                break;
        }
        alDistanceModel(alModel);
        checkError("setDistanceModel");
        currentSettings.distanceModel = model;
    }

    void AudioSystem::applySettings(const types::AudioSettings& settings) {
        if (!initialized) {
            loggerWarning("AudioSystem::applySettings called but system not initialized");
            return;
        }

        setMasterVolume(settings.masterVolume);
        setDopplerFactor(settings.dopplerFactor);
        setSpeedOfSound(settings.speedOfSound);
        setDistanceModel(settings.distanceModel);
        currentSettings.defaultRolloffFactor = settings.defaultRolloffFactor;

        loggerInfo("Audio settings applied - Master Volume: {}, Doppler: {}, Speed of Sound: {}",
            settings.masterVolume, settings.dopplerFactor, settings.speedOfSound);
    }

}
