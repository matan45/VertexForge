#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/EntityHandle.hpp"
#include "events/EventDispatcher.hpp"
#include "interfaces/IAudioService.hpp"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

namespace windows
{
    class SceneGraph : public controllers::imguiHandler::ImguiWindow
    {
    private:
        services::EntityHandle selectedHandle;
        services::EntityHandle lastSelectedHandle;
        events::SubscriptionToken sceneClearedToken;

        // Set of entity handles that need to be auto-expanded (parents of selected entity)
        std::unordered_set<uint64_t> expandedHandles;

        inline static std::unordered_map<std::string, std::vector<std::string>> submeshNameCache;
        
        inline static std::unordered_map<uint64_t, services::AudioHandle> audioPreviewHandles;

    public:
        explicit SceneGraph();
        ~SceneGraph() override;

        void draw() override;

    private:
        void drawEntityNode(services::EntityHandle handle);
        void drawDetails(services::EntityHandle handle);
        void dragDropEntity(services::EntityHandle handle);
        void subscribeToEvents();
        void onSceneCleared();

        void expandToSelection(services::EntityHandle handle);

        // Component drawing helpers
        void drawEntityName(services::EntityHandle handle, const std::string& currentName);
        void drawTransformComponent(services::EntityHandle handle);
        bool drawCameraComponent(services::EntityHandle handle);
        void drawIBLComponent(services::EntityHandle handle);
        bool drawMeshComponent(services::EntityHandle handle);
        void drawMaterialComponent(services::EntityHandle handle);
        bool drawAudioSource2DComponent(services::EntityHandle handle);
        bool drawAudioSource3DComponent(services::EntityHandle handle);
        void drawAddComponentButton(services::EntityHandle handle, bool hasCamera, bool hasMesh, bool hasAudio2D, bool hasAudio3D);

        // UI styling helpers
        static void pushComponentHeaderStyle();
        static void popComponentHeaderStyle();
        static void pushRemoveButtonStyle();
        static void popRemoveButtonStyle();
    };
}
