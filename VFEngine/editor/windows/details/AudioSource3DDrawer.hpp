#pragma once
#include "data/EntityHandle.hpp"
#include "interfaces/IAudioService.hpp"
#include <unordered_map>

namespace windows::details {

    class AudioSource3DDrawer {
    private:
        std::unordered_map<uint64_t, services::AudioHandle> audioPreviewHandles;
    public:
        bool draw(services::EntityHandle handle);
        void clearHandles();
        
    };

}
