#pragma once
#include "../../providers/render/IRenderTextureProvider.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../events/EventTypes.hpp"
#include <rendertexture/RenderTextureTypes.hpp>
#include <unordered_map>

namespace services
{
    enum class EditorMode : uint8_t;

    class RenderTexturePlayModeHandler
    {
    private:
        IRenderTextureProvider* provider = nullptr;
        ::events::SubscriptionToken editorModeChangedToken;

        // Maps entity handle to render texture ID
        std::unordered_map<EntityHandle, rendertexture::RenderTextureId, EntityHandle::Hash> activeTextures;
        bool rttActive = false;

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
    };
}
