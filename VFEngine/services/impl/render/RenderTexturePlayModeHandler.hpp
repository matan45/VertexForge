#pragma once
#include "../../providers/render/IRenderTextureProvider.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../events/EventTypes.hpp"
#include <rendertexture/RenderTextureTypes.hpp>
#include <entt/entt.hpp>
#include <atomic>
#include <unordered_map>

namespace services
{
    enum class EditorMode : uint8_t;

    class RenderTexturePlayModeHandler
    {
    private:
        IRenderTextureProvider* provider = nullptr;
        ::events::SubscriptionToken editorModeChangedToken;
        // The Runtime queues its startup scene as a DEFERRED load and publishes Edit->Play
        // immediately after, so enterPlayMode scans an empty registry and every scene-authored
        // render texture stays dark. This picks the scene up once it has actually landed.
        ::events::SubscriptionToken sceneLoadedToken;

        // Maps entity handle to render texture ID
        std::unordered_map<EntityHandle, rendertexture::RenderTextureId, EntityHandle::Hash> activeTextures;
        // Read from the notification handler (a frame-graph worker in the Runtime) as well as
        // the main thread that drives update().
        std::atomic<bool> rttActive{false};
        // Set by SceneLoadedNotification, consumed at the top of update(). The rescan creates
        // GPU render targets, so it must run on the same thread as the rest of the RTT work
        // rather than on whatever thread finished the scene load.
        std::atomic<bool> rescanPending{false};

    public:
        explicit RenderTexturePlayModeHandler(IRenderTextureProvider* provider);
        ~RenderTexturePlayModeHandler();

        RenderTexturePlayModeHandler(const RenderTexturePlayModeHandler&) = delete;
        RenderTexturePlayModeHandler& operator=(const RenderTexturePlayModeHandler&) = delete;

        void subscribeToEvents();
        void unsubscribeFromEvents();
        void update(float deltaTime);

    private:
        void onEditorModeChanged(EditorMode previousMode, EditorMode currentMode);
        void enterPlayMode();
        void exitPlayMode();
        // Creates the render target for one RTT entity and records it in activeTextures.
        // Returns false when the entity is not a renderable RTT (disabled, inactive, no camera)
        // or the provider refused the allocation.
        bool activateEntity(entt::entity entity);
        // Activates RTT entities that appeared after enterPlayMode ran (deferred scene load).
        void activatePendingEntities();
    };
}
