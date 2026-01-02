#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/EntityHandle.hpp"
#include "events/EventDispatcher.hpp"
#include "interfaces/IAudioService.hpp"
#include <unordered_map>
#include <string>
#include <vector>

namespace windows
{
    class EntityDetailsPanel : public controllers::imguiHandler::ImguiWindow
    {
    private:
        events::SubscriptionToken sceneClearedToken;

        inline static std::unordered_map<std::string, std::vector<std::string>> submeshNameCache;
        inline static std::unordered_map<uint64_t, services::AudioHandle> audioPreviewHandles;

    public:
        explicit EntityDetailsPanel();
        ~EntityDetailsPanel() override;

        void draw() override;

    private:
        void drawDetails(services::EntityHandle handle);

        // Component drawing helpers
        void drawEntityName(services::EntityHandle handle, const std::string& currentName);
        void drawEntityActiveCheckbox(services::EntityHandle handle, bool isActive);
        void drawTransformComponent(services::EntityHandle handle);
        bool drawCameraComponent(services::EntityHandle handle);
        void drawIBLComponent(services::EntityHandle handle);
        bool drawMeshComponent(services::EntityHandle handle);
        void drawMaterialComponent(services::EntityHandle handle);
        bool drawAudioSource2DComponent(services::EntityHandle handle);
        bool drawAudioSource3DComponent(services::EntityHandle handle);
        bool drawScriptComponent(services::EntityHandle handle);
        void drawAddComponentButton(services::EntityHandle handle, bool hasCamera, bool hasMesh, bool hasAudio2D, bool hasAudio3D, bool hasScript);

        // UI styling helpers
        static void pushComponentHeaderStyle();
        static void popComponentHeaderStyle();
        static void pushRemoveButtonStyle();
        static void popRemoveButtonStyle();

        void subscribeToEvents();
        void onSceneCleared();
    };
}
