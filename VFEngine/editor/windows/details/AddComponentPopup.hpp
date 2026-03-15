#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details {

    struct ComponentPresence
    {
        services::EntityHandle handle;
        bool hasCamera = false;
        bool hasMesh = false;
        bool hasAudio2D = false;
        bool hasAudio3D = false;
        bool hasScript = false;
        bool hasCollider = false;
        bool hasRigidBody = false;
        bool hasPhysicsAnimation = false;
        bool hasVFX = false;
        bool hasBillboard = false;
        bool hasText = false;
        bool hasDirectionalLight = false;
        bool hasPointLight = false;
        bool hasSpotLight = false;
        bool hasUICanvas = false;
        bool hasUIRect = false;
        bool hasUIImage = false;
        bool hasUILabel = false;
        bool hasUIScroll = false;
        bool hasUILayoutGroup = false;
        bool hasUIButton = false;
        bool hasUITextInput = false;
        bool hasUICheckbox = false;
        bool hasUIDropdown = false;
        bool hasUITabs = false;
        bool hasUISlider = false;
        bool hasUIProgressBar = false;
        bool hasSocketAttachment = false;
        bool hasNavmeshAgent = false;
        bool hasRenderTexture = false;
        bool hasController = false;
        bool hasIK = false;
        bool hasBehaviorTree = false;
        bool hasDecal = false;
        bool hasReverbZone = false;
        bool hasUIAnimation = false;
        bool hasUIMask = false;
        bool hasUIDraggable = false;
        bool hasUIDropTarget = false;
    };

    class AddComponentPopup {
    public:
        void draw(const ComponentPresence& components);

    private:
        void drawGeneralSection(const ComponentPresence& c);
        void drawPhysicsSection(const ComponentPresence& c);
        void drawAnimationSection(const ComponentPresence& c);
        void drawLightingSection(const ComponentPresence& c);
        void drawUISection(const ComponentPresence& c);
        void drawPluginSection(const ComponentPresence& c);
    };

}
