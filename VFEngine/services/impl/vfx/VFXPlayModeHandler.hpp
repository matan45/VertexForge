#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../data/EditorMode.hpp"
#include "../../data/VFXTypes.hpp"
#include "../../events/EventDispatcher.hpp"
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace services
{
    class IVFXRuntimeProvider;

    class VFXPlayModeHandler
    {
    private:
        IVFXRuntimeProvider* vfxProvider = nullptr;
        ::events::SubscriptionToken editorModeChangedToken;
        ::events::SubscriptionToken transformChangedToken;
        ::events::SubscriptionToken sectorLoadedToken;
        ::events::SubscriptionToken sectorUnloadedToken;

        // Maps entity handle to VFX runtime instance ID
        std::unordered_map<EntityHandle, VFXInstanceId, EntityHandle::Hash> activeVFXInstances;
        bool vfxActive = false;

        // Streaming budget: max emitter creates per frame to avoid spikes
        static constexpr uint32_t MAX_STREAMING_CREATES_PER_FRAME = 4;
        struct PendingStreamCreate
        {
            EntityHandle entity;
            std::string vfxPath;
            glm::mat4 worldTransform{1.0f};
            bool loop = true;
            uint8_t priority = 2;
            bool cameraRelative = false;
            bool autoPlay = true;
        };
        std::vector<PendingStreamCreate> pendingStreamCreates;

    public:
        explicit VFXPlayModeHandler(IVFXRuntimeProvider* vfxProvider);
        ~VFXPlayModeHandler();

        VFXPlayModeHandler(const VFXPlayModeHandler&) = delete;
        VFXPlayModeHandler& operator=(const VFXPlayModeHandler&) = delete;

        void subscribeToEvents();
        void unsubscribeFromEvents();

        void update(float deltaTime);

    private:
        void onEditorModeChanged(EditorMode previousMode, EditorMode currentMode);
        void onTransformChanged(EntityHandle entity);
        void onSectorLoaded(int32_t coordX, int32_t coordZ);
        void onSectorUnloaded(int32_t coordX, int32_t coordZ);
        void processPendingStreamCreates();
        void enterPlayMode();
        void exitPlayMode();
    };
}
