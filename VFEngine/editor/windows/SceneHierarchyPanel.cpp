#include "SceneHierarchyPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/SculptModeEvents.hpp"
#include <imgui.h>

namespace windows
{
    SceneHierarchyPanel::SceneHierarchyPanel()
    {
        subscribeToEvents();
    }

    SceneHierarchyPanel::~SceneHierarchyPanel()
    {
        events::EventDispatcher::instance().unsubscribe(sceneClearedToken);
    }

    void SceneHierarchyPanel::subscribeToEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                onSceneCleared();
            });
    }

    void SceneHierarchyPanel::onSceneCleared()
    {
        selectedHandle = services::EntityHandle::invalid();
        expandedHandles.clear();
    }

    void SceneHierarchyPanel::draw()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Sync local selection with global selection (e.g., from viewport picking)
        events::scene::GetSelectedEntityQuery selectedQuery;
        auto globalSelected = dispatcher.query(selectedQuery);
        selectedHandle = globalSelected.value_or(services::EntityHandle::invalid());

        // Detect selection change and auto-expand to show selected entity
        if (selectedHandle.id != lastSelectedHandle.id && selectedHandle.isValid())
        {
            expandToSelection(selectedHandle);
        }
        lastSelectedHandle = selectedHandle;

        if (ImGui::Begin("SceneGraph"))
        {
            // Query root entity through event system
            events::scene::GetSceneHierarchyQuery hierarchyQuery;
            auto hierarchy = dispatcher.query(hierarchyQuery);

            if (!hierarchy.entities.empty())
            {
                auto rootHandle = hierarchy.entities[0].handle;
                drawEntityNode(rootHandle);
            }

            // Right-click context menu for adding/removing entities
            if (ImGui::BeginPopupContextWindow())
            {
                if (ImGui::MenuItem("Add New Entity"))
                {
                    events::scene::GetSceneHierarchyQuery rootQuery;
                    auto rootHierarchy = dispatcher.query(rootQuery);
                    services::EntityHandle parentHandle = selectedHandle.isValid()
                                                              ? selectedHandle
                                                              : (rootHierarchy.entities.empty()
                                                                     ? services::EntityHandle::invalid()
                                                                     : rootHierarchy.entities[0].handle);

                    events::scene::CreateEntityCommand cmd;
                    cmd.name = "New Entity";
                    cmd.parent = parentHandle.isValid() ? std::optional{parentHandle} : std::nullopt;
                    dispatcher.execute(cmd);
                }

                // Prevent deleting/duplicating the root entity
                if (selectedHandle.isValid())
                {
                    events::scene::GetSceneHierarchyQuery rootCheckQuery;
                    auto rootCheckHierarchy = dispatcher.query(rootCheckQuery);
                    bool isRoot = !rootCheckHierarchy.entities.empty() &&
                        rootCheckHierarchy.entities[0].handle.id == selectedHandle.id;

                    if (!isRoot)
                    {
                        if (ImGui::MenuItem("Duplicate"))
                        {
                            events::scene::DuplicateEntityCommand cmd;
                            cmd.entity = selectedHandle;
                            auto duplicated = dispatcher.execute(cmd);
                            if (duplicated.isValid())
                            {
                                selectedHandle = duplicated;
                                events::scene::SelectEntityCommand selectCmd;
                                selectCmd.entity = duplicated;
                                dispatcher.execute(selectCmd);
                            }
                        }

                        if (ImGui::MenuItem("Remove Selected Entity"))
                        {
                            events::scene::DeleteEntityCommand cmd;
                            cmd.entity = selectedHandle;
                            dispatcher.execute(cmd);
                            selectedHandle = services::EntityHandle::invalid();
                        }
                    }
                }

                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }

    void SceneHierarchyPanel::drawEntityNode(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::PushID(static_cast<int>(handle.id));

        events::scene::GetEntityQuery entityQuery;
        entityQuery.entity = handle;
        auto entityDataOpt = dispatcher.query(entityQuery);

        static const std::string unknownName = "Unknown";
        const std::string& entityName = entityDataOpt.has_value() ? entityDataOpt->name : unknownName;

        if (expandedHandles.count(handle.id) > 0)
        {
            ImGui::SetNextItemOpen(true);
            expandedHandles.erase(handle.id); // Only expand once
        }

        ImGuiTreeNodeFlags flags = (selectedHandle.id == handle.id) ? ImGuiTreeNodeFlags_Selected : 0;
        flags |= ImGuiTreeNodeFlags_OpenOnArrow;

        bool nodeOpen = ImGui::TreeNodeEx((void*)handle.id, flags, "%s", entityName.c_str());

        // Select the entity when clicked (blocked during sculpt mode)
        if (ImGui::IsItemClicked())
        {
            bool isSculptMode = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});
            if (!isSculptMode)
            {
                selectedHandle = handle;

                events::scene::SelectEntityCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
        }

        if (ImGui::BeginDragDropSource())
        {
            // Use single payload type for both entity reparenting and prefab creation
            ImGui::SetDragDropPayload("DND_SCENE_ENTITY", &handle, sizeof(services::EntityHandle));
            ImGui::Text("Move %s", entityName.c_str());
            ImGui::EndDragDropSource();
        }

        dragDropEntity(handle);

        if (nodeOpen)
        {
            if (entityDataOpt.has_value())
            {
                for (const auto& child : entityDataOpt->children)
                {
                    drawEntityNode(child);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneHierarchyPanel::dragDropEntity(services::EntityHandle handle)
    {
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SCENE_ENTITY"))
            {
                services::EntityHandle draggedHandle = *(services::EntityHandle*)payload->Data;

                if (draggedHandle.id != handle.id)
                {
                    auto& dispatcher = events::EventDispatcher::instance();

                    events::scene::ReparentEntityCommand cmd;
                    cmd.entity = draggedHandle;
                    cmd.newParent = handle;
                    dispatcher.execute(cmd);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    void SceneHierarchyPanel::expandToSelection(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        expandedHandles.clear();

        // Walk up the parent chain and collect all ancestors
        services::EntityHandle current = handle;
        while (current.isValid())
        {
            events::scene::GetEntityQuery entityQuery;
            entityQuery.entity = current;
            auto entityDataOpt = dispatcher.query(entityQuery);

            if (!entityDataOpt.has_value() || !entityDataOpt->parent.has_value())
            {
                break; // Reached root or invalid entity
            }

            services::EntityHandle parentHandle = entityDataOpt->parent.value();
            expandedHandles.insert(parentHandle.id);

            current = parentHandle;
        }
    }
}
