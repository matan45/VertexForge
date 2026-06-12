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
        bool hasOffMeshLink = false;
        bool hasNavmeshObstacle = false;
        bool hasNavmeshModifierVolume = false;
        bool hasRenderTexture = false;
        bool hasController = false;
        bool hasIK = false;
        bool hasBehaviorTree = false;
        bool hasDecal = false;
        bool hasReverbZone = false;
        bool hasFogVolume = false;
        bool hasUIAnimation = false;
        bool hasUIMask = false;
        bool hasUIDraggable = false;
        bool hasUIDropTarget = false;
        bool hasNavInvoker = false;
        bool hasVolumetricNavVolume = false;
        bool hasVolumetricAgent = false;
        bool hasDestructible = false;
        bool hasBuoyancy = false;
        bool hasUIStyle = false;
        bool hasUITooltip = false;
        bool hasUIWindow = false;
    };

    class AddComponentPopup {
    public:
        void draw(const ComponentPresence& components);

    private:
        char searchBuffer[128] = {};

        void drawGeneralSection(const ComponentPresence& c, const char* filter = nullptr);
        void drawPhysicsSection(const ComponentPresence& c, const char* filter = nullptr);
        void drawAnimationSection(const ComponentPresence& c, const char* filter = nullptr);
        void drawLightingSection(const ComponentPresence& c, const char* filter = nullptr);
        void drawUISection(const ComponentPresence& c, const char* filter = nullptr);
        void drawPluginSection(const ComponentPresence& c, const char* filter = nullptr);

        static bool matchesFilter(const char* label, const char* filter);
    };

}
