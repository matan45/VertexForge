#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class AddComponentPopup {
    public:
        void draw(services::EntityHandle handle, bool hasCamera, bool hasMesh,
                  bool hasAudio2D, bool hasAudio3D, bool hasScript,
                  bool hasCollider, bool hasRigidBody, bool hasVFX, bool hasBillboard,
                  bool hasText, bool hasDirectionalLight, bool hasPointLight, bool hasSpotLight,
                  bool hasUICanvas);
    };

}
