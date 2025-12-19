#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/EntityHandle.hpp"
#include "events/EventDispatcher.hpp"
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
        
        void expandToSelection(services::EntityHandle handle);

        // UI styling helpers
        static void pushComponentHeaderStyle();
        static void popComponentHeaderStyle();
        static void pushRemoveButtonStyle();
        static void popRemoveButtonStyle();
    };
}
