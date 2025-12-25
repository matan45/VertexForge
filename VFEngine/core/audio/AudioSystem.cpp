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

}
