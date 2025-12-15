#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/EntityHandle.hpp"
#include "events/EventDispatcher.hpp"
#include <unordered_map>
#include <string>
#include <vector>

namespace windows
{
    class SceneGraph : public controllers::imguiHandler::ImguiWindow
    {
    private:
        services::EntityHandle selectedHandle;
        events::SubscriptionToken sceneClearedToken;

        // Cache for submesh names keyed by mesh path
        static std::unordered_map<std::string, std::vector<std::string>> submeshNameCache;

    public:
        SceneGraph();
        ~SceneGraph() override;

        void draw() override;

    private:
        void drawEntityNode(services::EntityHandle handle);
        void drawDetails(services::EntityHandle handle);
        void dragDropEntity(services::EntityHandle handle);
        void subscribeToEvents();
        void onSceneCleared();

        // UI styling helpers
        static void pushComponentHeaderStyle();
        static void popComponentHeaderStyle();
        static void pushRemoveButtonStyle();
        static void popRemoveButtonStyle();
    };
}
