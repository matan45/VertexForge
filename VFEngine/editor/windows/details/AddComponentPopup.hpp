#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    class AddComponentPopup {
    public:
        void draw(services::EntityHandle handle, bool hasCamera, bool hasMesh,
                  bool hasAudio2D, bool hasAudio3D, bool hasScript,
                  bool hasCollider, bool hasRigidBody, bool hasPhysicsAnimation,
                  bool hasVFX, bool hasBillboard,
                  bool hasText, bool hasDirectionalLight, bool hasPointLight, bool hasSpotLight,
                  bool hasUICanvas, bool hasUIRect, bool hasUIImage, bool hasUILabel,
                  bool hasUIScroll, bool hasUILayoutGroup, bool hasUIButton,
                  bool hasUITextInput, bool hasUICheckbox, bool hasUIDropdown,
                  bool hasUITabs, bool hasUISlider, bool hasUIProgressBar);
    };

}
