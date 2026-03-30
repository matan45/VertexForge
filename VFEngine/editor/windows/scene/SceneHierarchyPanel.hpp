#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "events/EventDispatcher.hpp"
#include <unordered_set>

namespace windows
{
    class SceneHierarchyPanel : public controllers::imguiHandler::ImguiWindow
    {
    private:
        services::EntityHandle selectedHandle;
        services::EntityHandle lastSelectedHandle;
        events::SubscriptionToken sceneClearedToken;

        std::unordered_set<uint64_t> expandedHandles;
        std::unordered_set<uint64_t> hiddenEntities;
        std::unordered_set<uint64_t> lockedEntities;
        int entityTypeFilter = 0;

    public:
        explicit SceneHierarchyPanel();
        ~SceneHierarchyPanel() override;

        void draw() override;

    private:
        void drawEntityNode(services::EntityHandle handle);
        void dragDropEntity(services::EntityHandle handle);
        void subscribeToEvents();
        void onSceneCleared();
        void expandToSelection(services::EntityHandle handle);
        const char* getEntityIcon(const services::EntityData& data) const;
        bool matchesTypeFilter(const services::EntityData& data) const;
    };
}
