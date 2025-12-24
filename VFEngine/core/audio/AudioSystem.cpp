#include "AudioSystem.hpp"
#include <spdlog/spdlog.h>

namespace core::audio {

    AudioSystem::~AudioSystem() {
        if (initialized) {
            cleanUp();
        }
    }

    bool AudioSystem::init() {
        if (initialized) {
            spdlog::warn("AudioSystem already initialized");
            return true;
        }

        device = alcOpenDevice(nullptr);
        if (!device) {
            spdlog::error("Failed to open default OpenAL device");
            return false;
        }

        context = alcCreateContext(device, nullptr);
        if (!context) {
            spdlog::error("Failed to create OpenAL context");
            alcCloseDevice(device);
            device = nullptr;
            return false;
        }

        if (!alcMakeContextCurrent(context)) {
            spdlog::error("Failed to make OpenAL context current");
            alcDestroyContext(context);
            alcCloseDevice(device);
            context = nullptr;
            device = nullptr;
            return false;
        }

        alGetError();

        setDistanceModel(currentDistanceModel);
        setDopplerFactor(dopplerFactor);
        setSpeedOfSound(speedOfSound);

        initialized = true;

        spdlog::info("AudioSystem initialized successfully");
        spdlog::info("  Device: {}", getDeviceName());
        spdlog::info("  Vendor: {}", getVendor());
        spdlog::info("  Version: {}", getVersion());
        spdlog::info("  Renderer: {}", getRenderer());

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
        spdlog::info("AudioSystem cleaned up");
    }

    void AudioSystem::setDistanceModel(DistanceModel model) {
        if (!initialized) return;

        ALenum alModel;
        switch (model) {
            case DistanceModel::InverseDistance:
                alModel = AL_INVERSE_DISTANCE;
                break;
            case DistanceModel::InverseDistanceClamped:
                alModel = AL_INVERSE_DISTANCE_CLAMPED;
                break;
            case DistanceModel::LinearDistance:
                alModel = AL_LINEAR_DISTANCE;
                break;
            case DistanceModel::LinearDistanceClamped:
                alModel = AL_LINEAR_DISTANCE_CLAMPED;
                break;
            case DistanceModel::ExponentDistance:
                alModel = AL_EXPONENT_DISTANCE;
                break;
            case DistanceModel::ExponentDistanceClamped:
                alModel = AL_EXPONENT_DISTANCE_CLAMPED;
                break;
            case DistanceModel::None:
            default:
                alModel = AL_NONE;
                break;
        }

        alDistanceModel(alModel);
        currentDistanceModel = model;
        checkError("setDistanceModel");
    }

    void AudioSystem::setDopplerFactor(float factor) {
        if (!initialized) return;
        alDopplerFactor(factor);
        dopplerFactor = factor;
        checkError("setDopplerFactor");
    }

    void AudioSystem::setSpeedOfSound(float speed) {
        if (!initialized) return;
        alSpeedOfSound(speed);
        speedOfSound = speed;
        checkError("setSpeedOfSound");
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
            spdlog::error("OpenAL error in {}: {} (0x{:X})", operation, errorStr, error);
            return true;
        }
        return false;
    }

}
