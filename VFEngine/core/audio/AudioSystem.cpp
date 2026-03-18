#include "AudioSystem.hpp"
#include <AL/al.h>
#include "print/Log.hpp"

namespace core::audio {

    bool AudioSystem::s_efxAvailable = false;
    LPALGENFILTERS AudioSystem::alGenFilters = nullptr;
    LPALDELETEFILTERS AudioSystem::alDeleteFilters = nullptr;
    LPALFILTERI AudioSystem::alFilteri = nullptr;
    LPALFILTERF AudioSystem::alFilterf = nullptr;

    LPALGENEFFECTS AudioSystem::alGenEffects = nullptr;
    LPALDELETEEFFECTS AudioSystem::alDeleteEffects = nullptr;
    LPALEFFECTI AudioSystem::alEffecti = nullptr;
    LPALEFFECTF AudioSystem::alEffectf = nullptr;
    LPALEFFECTFV AudioSystem::alEffectfv = nullptr;
    LPALGENAUXILIARYEFFECTSLOTS AudioSystem::alGenAuxiliaryEffectSlots = nullptr;
    LPALDELETEAUXILIARYEFFECTSLOTS AudioSystem::alDeleteAuxiliaryEffectSlots = nullptr;
    LPALAUXILIARYEFFECTSLOTI AudioSystem::alAuxiliaryEffectSloti = nullptr;
    LPALAUXILIARYEFFECTSLOTF AudioSystem::alAuxiliaryEffectSlotf = nullptr;

    AudioSystem::~AudioSystem() {
        if (initialized) {
            cleanUp();
        }
    }

    bool AudioSystem::init() {
        if (initialized) {
            return true;
        }

        device = alcOpenDevice(nullptr);
        if (!device) {
            vfLogError("Failed to open default OpenAL device");
            return false;
        }

        context = alcCreateContext(device, nullptr);
        if (!context) {
            vfLogError("Failed to create OpenAL context");
            alcCloseDevice(device);
            device = nullptr;
            return false;
        }

        if (!alcMakeContextCurrent(context)) {
            vfLogError("Failed to make OpenAL context current");
            alcDestroyContext(context);
            alcCloseDevice(device);
            context = nullptr;
            device = nullptr;
            return false;
        }

        alGetError();

        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);

        efxSupported = alcIsExtensionPresent(device, "ALC_EXT_EFX") == ALC_TRUE;
        if (efxSupported)
        {
            alGenFilters = reinterpret_cast<LPALGENFILTERS>(alGetProcAddress("alGenFilters"));
            alDeleteFilters = reinterpret_cast<LPALDELETEFILTERS>(alGetProcAddress("alDeleteFilters"));
            alFilteri = reinterpret_cast<LPALFILTERI>(alGetProcAddress("alFilteri"));
            alFilterf = reinterpret_cast<LPALFILTERF>(alGetProcAddress("alFilterf"));

            // Load effect function pointers
            alGenEffects = reinterpret_cast<LPALGENEFFECTS>(alGetProcAddress("alGenEffects"));
            alDeleteEffects = reinterpret_cast<LPALDELETEEFFECTS>(alGetProcAddress("alDeleteEffects"));
            alEffecti = reinterpret_cast<LPALEFFECTI>(alGetProcAddress("alEffecti"));
            alEffectf = reinterpret_cast<LPALEFFECTF>(alGetProcAddress("alEffectf"));
            alEffectfv = reinterpret_cast<LPALEFFECTFV>(alGetProcAddress("alEffectfv"));
            alGenAuxiliaryEffectSlots = reinterpret_cast<LPALGENAUXILIARYEFFECTSLOTS>(alGetProcAddress("alGenAuxiliaryEffectSlots"));
            alDeleteAuxiliaryEffectSlots = reinterpret_cast<LPALDELETEAUXILIARYEFFECTSLOTS>(alGetProcAddress("alDeleteAuxiliaryEffectSlots"));
            alAuxiliaryEffectSloti = reinterpret_cast<LPALAUXILIARYEFFECTSLOTI>(alGetProcAddress("alAuxiliaryEffectSloti"));
            alAuxiliaryEffectSlotf = reinterpret_cast<LPALAUXILIARYEFFECTSLOTF>(alGetProcAddress("alAuxiliaryEffectSlotf"));

            if (!alGenFilters || !alDeleteFilters || !alFilteri || !alFilterf ||
                !alGenEffects || !alDeleteEffects || !alEffecti || !alEffectf || !alEffectfv ||
                !alGenAuxiliaryEffectSlots || !alDeleteAuxiliaryEffectSlots ||
                !alAuxiliaryEffectSloti || !alAuxiliaryEffectSlotf)
            {
                vfLogWarning("OpenAL EFX extension present but failed to load EFX functions");
                efxSupported = false;
                alGenFilters = nullptr;
                alDeleteFilters = nullptr;
                alFilteri = nullptr;
                alFilterf = nullptr;
                alGenEffects = nullptr;
                alDeleteEffects = nullptr;
                alEffecti = nullptr;
                alEffectf = nullptr;
                alEffectfv = nullptr;
                alGenAuxiliaryEffectSlots = nullptr;
                alDeleteAuxiliaryEffectSlots = nullptr;
                alAuxiliaryEffectSloti = nullptr;
                alAuxiliaryEffectSlotf = nullptr;
            }
            else
            {
                // Query max auxiliary sends
                alcGetIntegerv(device, ALC_MAX_AUXILIARY_SENDS, 1, &maxAuxiliarySends);
                vfLogInfo("OpenAL EFX extension supported - max auxiliary sends: {}", maxAuxiliarySends);
            }
        }
        else
        {
            vfLogWarning("OpenAL EFX extension not available - distance filtering disabled");
        }
        s_efxAvailable = efxSupported;

        initialized = true;

        vfLogInfo("AudioSystem initialized: {}", getDeviceName());

        return true;
    }

    void AudioSystem::releaseContext()
    {
        alcMakeContextCurrent(nullptr);
    }

    void AudioSystem::acquireContext()
    {
        if (context)
        {
            alcMakeContextCurrent(context);
        }
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
            vfLogError("OpenAL error in {}: {} (0x{:X})", operation, errorStr, error);
            return true;
        }
        return false;
    }

    void AudioSystem::setMasterVolume(float volume) {
        if (!initialized) {
            vfLogWarning("AudioSystem::setMasterVolume called but system not initialized");
            return;
        }
        alListenerf(AL_GAIN, volume);
        checkError("setMasterVolume");
        currentSettings.masterVolume = volume;
    }

    void AudioSystem::setDopplerFactor(float factor) {
        if (!initialized) {
            vfLogWarning("AudioSystem::setDopplerFactor called but system not initialized");
            return;
        }
        alDopplerFactor(factor);
        checkError("setDopplerFactor");
        currentSettings.dopplerFactor = factor;
    }

    void AudioSystem::setSpeedOfSound(float speed) {
        if (!initialized) {
            vfLogWarning("AudioSystem::setSpeedOfSound called but system not initialized");
            return;
        }
        alSpeedOfSound(speed);
        checkError("setSpeedOfSound");
        currentSettings.speedOfSound = speed;
    }

    void AudioSystem::setDistanceModel(types::AudioDistanceModel model) {
        if (!initialized) {
            vfLogWarning("AudioSystem::setDistanceModel called but system not initialized");
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
            vfLogWarning("AudioSystem::applySettings called but system not initialized");
            return;
        }

        setMasterVolume(settings.masterVolume);
        setDopplerFactor(settings.dopplerFactor);
        setSpeedOfSound(settings.speedOfSound);
        setDistanceModel(settings.distanceModel);
        currentSettings.defaultRolloffFactor = settings.defaultRolloffFactor;

        vfLogInfo("Audio settings applied - Master Volume: {}, Doppler: {}, Speed of Sound: {}",
            settings.masterVolume, settings.dopplerFactor, settings.speedOfSound);
    }

}
