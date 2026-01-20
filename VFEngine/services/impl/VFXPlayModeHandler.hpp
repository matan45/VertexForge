#pragma once

#include "../data/EntityHandle.hpp"
#include "../data/EditorMode.hpp"
#include "../data/VFXTypes.hpp"
#include "../events/EventDispatcher.hpp"
#include <unordered_map>

namespace services
{
    class IVFXRuntimeProvider;  // Forward declaration

    class VFXPlayModeHandler
    {
    private:
        IVFXRuntimeProvider* vfxProvider = nullptr;
        ::events::SubscriptionToken editorModeChangedToken;
        ::events::SubscriptionToken transformChangedToken;

        // Maps entity handle to VFX runtime instance ID
        std::unordered_map<EntityHandle, VFXInstanceId, EntityHandle::Hash> activeVFXInstances;
        bool vfxActive = false;

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
        void enterPlayMode();
        void exitPlayMode();
    };
}
