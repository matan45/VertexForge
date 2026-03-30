#include "SceneHierarchyPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include <IconsFontAwesome6.h>
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
        hiddenEntities.clear();
        lockedEntities.clear();
    }

    const char* SceneHierarchyPanel::getEntityIcon(const services::EntityData& data) const
    {
        using CT = services::ComponentTypeId;
        if (data.hasComponent(CT::Camera)) return ICON_FA_VIDEO;
        if (data.hasComponent(CT::DirectionalLight)) return ICON_FA_SUN;
        if (data.hasComponent(CT::PointLight)) return ICON_FA_LIGHTBULB;
        if (data.hasComponent(CT::SpotLight)) return ICON_FA_BOLT;
        if (data.hasComponent(CT::Mesh)) return ICON_FA_CUBE;
        if (data.hasComponent(CT::AudioSource2D) || data.hasComponent(CT::AudioSource3D)) return ICON_FA_VOLUME_HIGH;
        if (data.hasComponent(CT::VFX)) return ICON_FA_FIRE;
        if (data.hasComponent(CT::Script)) return ICON_FA_CODE;
        if (data.hasComponent(CT::Text)) return ICON_FA_FONT;
        if (data.hasComponent(CT::Decal)) return ICON_FA_STAMP;
        if (data.hasComponent(CT::NavmeshAgent)) return ICON_FA_ROUTE;
        return ICON_FA_CIRCLE_DOT;
    }

    bool SceneHierarchyPanel::matchesTypeFilter(const services::EntityData& data) const
    {
        using CT = services::ComponentTypeId;
        switch (entityTypeFilter)
        {
        case 0: return true; // All
        case 1: return data.hasComponent(CT::Camera);
        case 2: return data.hasComponent(CT::DirectionalLight) || data.hasComponent(CT::PointLight) || data.hasComponent(CT::SpotLight);
        case 3: return data.hasComponent(CT::Mesh);
        case 4: return data.hasComponent(CT::AudioSource2D) || data.hasComponent(CT::AudioSource3D);
        case 5: return data.hasComponent(CT::VFX);
        case 6: return data.hasComponent(CT::Script);
        default: return true;
        }
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
            // Type filter dropdown
            const char* filterOptions[] = {"All", "Camera", "Light", "Mesh", "Audio", "VFX", "Script"};
            ImGui::SetNextItemWidth(120.0f);
            ImGui::Combo("##TypeFilter", &entityTypeFilter, filterOptions, 7);
            ImGui::Separator();

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

        // Type filter: skip non-matching entities but recurse children
        bool matchesFilter = !entityDataOpt.has_value() || matchesTypeFilter(*entityDataOpt);
        if (!matchesFilter && entityTypeFilter != 0)
        {
            // Still recurse children to find matching descendants
            if (entityDataOpt.has_value())
            {
                for (const auto& child : entityDataOpt->children)
                    drawEntityNode(child);
            }
            ImGui::PopID();
            return;
        }

        if (expandedHandles.count(handle.id) > 0)
        {
            ImGui::SetNextItemOpen(true);
            expandedHandles.erase(handle.id);
        }

        ImGuiTreeNodeFlags flags = (selectedHandle.id == handle.id) ? ImGuiTreeNodeFlags_Selected : 0;
        flags |= ImGuiTreeNodeFlags_OpenOnArrow;

        // Entity icon
        const char* icon = entityDataOpt.has_value() ? getEntityIcon(*entityDataOpt) : ICON_FA_CIRCLE_DOT;
        char label[256];
        snprintf(label, sizeof(label), "%s  %s", icon, entityName.c_str());

        bool nodeOpen = ImGui::TreeNodeEx((void*)handle.id, flags, "%s", label);

        // Select entity when tree node is clicked
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
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

        // Visibility toggle (eye icon)
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 40.0f);
        bool isHidden = hiddenEntities.count(handle.id) > 0;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::SmallButton(isHidden ? ICON_FA_EYE_SLASH : ICON_FA_EYE))
        {
            if (isHidden)
                hiddenEntities.erase(handle.id);
            else
                hiddenEntities.insert(handle.id);

            events::scene::SetEntityActiveCommand cmd;
            cmd.entity = handle;
            cmd.isActive = isHidden;
            dispatcher.execute(cmd);
        }
        ImGui::PopStyleColor();

        // Lock toggle
        ImGui::SameLine();
        bool isLocked = lockedEntities.count(handle.id) > 0;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::SmallButton(isLocked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN))
        {
            if (isLocked)
                lockedEntities.erase(handle.id);
            else
                lockedEntities.insert(handle.id);
        }
        ImGui::PopStyleColor();

        if (!isLocked && ImGui::BeginDragDropSource())
        {
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
