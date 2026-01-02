#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class AddComponentPopup {
    public:
        void draw(services::EntityHandle handle, bool hasCamera, bool hasMesh,
                  bool hasAudio2D, bool hasAudio3D, bool hasScript);
    };

}
