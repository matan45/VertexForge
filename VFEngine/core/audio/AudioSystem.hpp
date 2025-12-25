#pragma once
#include <AL/al.h>
#include <AL/alc.h>
#include <string>

namespace core::audio
{
    class AudioSystem
    {
    private:
        ALCdevice* device = nullptr;
        ALCcontext* context = nullptr;
        bool initialized = false;

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

        static bool checkError(const char* operation);
    };
}
