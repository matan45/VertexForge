#include "SceneHierarchyPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/render/MaterialEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "asset/AssetRef.hpp"
#include "../../dragdrop/DragDropManager.hpp"
#include "../../selection/SelectionPolicy.hpp"
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <filesystem>

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
        selectedHandles.clear();
        expandedHandles.clear();
        hiddenEntities.clear();
        lockedEntities.clear();
        visibleOrder.clear();
        lastVisibleOrder.clear();
        rootSeeded = false;
        renamingHandle = services::EntityHandle::INVALID_ID;
        rangeAnchorHandle = services::EntityHandle::INVALID_ID;
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

    bool SceneHierarchyPanel::matchesNameSearch(const std::string& name) const
    {
        if (nameSearchBuffer[0] == '\0')
        {
            return true;
        }

        auto toLower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
        std::string haystack(name);
        std::string needle(nameSearchBuffer);
        std::transform(haystack.begin(), haystack.end(), haystack.begin(), toLower);
        std::transform(needle.begin(), needle.end(), needle.begin(), toLower);
        return haystack.find(needle) != std::string::npos;
    }

    bool SceneHierarchyPanel::isSelected(services::EntityHandle handle) const
    {
        for (const auto& selected : selectedHandles)
        {
            if (selected.id == handle.id) return true;
        }
        return false;
    }

    bool SceneHierarchyPanel::isEntityDragActive() const
    {
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        return payload != nullptr && payload->IsDataType("DND_SCENE_ENTITY");
    }

    void SceneHierarchyPanel::draw()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Sync local selection with global selection (e.g., from viewport picking)
        selectedHandles = dispatcher.query(events::scene::GetSelectedEntitiesQuery{});
        selectedHandle = selectedHandles.empty() ? services::EntityHandle::invalid() : selectedHandles.front();

        // Detect selection change and auto-expand to show selected entity
        if (selectedHandle.id != lastSelectedHandle.id && selectedHandle.isValid())
        {
            expandToSelection(selectedHandle);
        }
        lastSelectedHandle = selectedHandle;

        // Previous frame's draw order is the basis for shift-range selection
        lastVisibleOrder.swap(visibleOrder);
        visibleOrder.clear();

        if (ImGui::Begin("SceneGraph"))
        {
            events::scene::GetSceneHierarchyQuery hierarchyQuery;
            auto hierarchy = dispatcher.query(hierarchyQuery);

            drawToolbar(hierarchy);
            ImGui::Separator();

            if (!hierarchy.entities.empty())
            {
                auto rootHandle = hierarchy.entities[0].handle;

                // Root starts expanded on a fresh scene
                if (!rootSeeded)
                {
                    expandedHandles.insert(rootHandle.id);
                    rootSeeded = true;
                }

                if (nameSearchBuffer[0] != '\0')
                {
                    drawSearchResults(hierarchy);
                }
                else
                {
                    drawEntityNode(rootHandle);
                }
            }

            drawContextMenu(hierarchy);

            // Drop target on empty space - reparent entity to root
            if (!hierarchy.entities.empty() && nameSearchBuffer[0] == '\0')
            {
                auto rootHandle = hierarchy.entities[0].handle;

                ImVec2 windowPos = ImGui::GetWindowPos();
                ImVec2 regionMin = ImGui::GetWindowContentRegionMin();
                ImVec2 regionMax = ImGui::GetWindowContentRegionMax();
                ImRect emptySpaceRect(
                    ImVec2(windowPos.x + regionMin.x, windowPos.y + regionMin.y),
                    ImVec2(windowPos.x + regionMax.x, windowPos.y + regionMax.y)
                );

                if (ImGui::BeginDragDropTargetCustom(emptySpaceRect, ImGui::GetID("SceneGraphRootDrop")))
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SCENE_ENTITY"))
                    {
                        services::EntityHandle draggedHandle = *(services::EntityHandle*)payload->Data;
                        if (draggedHandle.id != rootHandle.id)
                        {
                            events::scene::ReparentEntityCommand cmd;
                            cmd.entity = draggedHandle;
                            cmd.newParent = rootHandle;
                            dispatcher.execute(cmd);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            handleShortcuts();
        }
        ImGui::End();
    }

    void SceneHierarchyPanel::drawToolbar(const services::SceneHierarchyData& hierarchy)
    {
        ImGui::SetNextItemWidth(150.0f);
        ImGui::InputTextWithHint("##NameSearch", ICON_FA_MAGNIFYING_GLASS " Search...",
                                 nameSearchBuffer, sizeof(nameSearchBuffer));

        ImGui::SameLine();
        const char* filterOptions[] = {"All", "Camera", "Light", "Mesh", "Audio", "VFX", "Script"};
        ImGui::SetNextItemWidth(90.0f);
        ImGui::Combo("##TypeFilter", &entityTypeFilter, filterOptions, 7);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::SmallButton(ICON_FA_ANGLES_DOWN))
        {
            expandAll(hierarchy);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Expand All");

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_ANGLES_UP))
        {
            collapseAll();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Collapse All");
        ImGui::PopStyleColor();
    }

    void SceneHierarchyPanel::expandAll(const services::SceneHierarchyData& hierarchy)
    {
        for (const auto& entity : hierarchy.entities)
        {
            if (!entity.children.empty())
            {
                expandedHandles.insert(entity.handle.id);
            }
        }
    }

    void SceneHierarchyPanel::collapseAll()
    {
        expandedHandles.clear();
        rootSeeded = false; // re-seed so the root row itself stays open
    }

    void SceneHierarchyPanel::handleSelectionClick(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // VK-1490: selection input is edit-mode only. The gate sits on the input
        // handler, not on the commands — Stop still restores the selection
        // through SelectEntitiesCommand while transitioning back to Edit.
        if (dispatcher.query(events::editor::IsPlayModeQuery{})) return;

        const ImGuiIO& io = ImGui::GetIO();

        if (io.KeyShift)
        {
            // Range over last frame's visible order, anchor..clicked inclusive
            auto range = selection::computeRangeSelection(
                lastVisibleOrder, services::EntityHandle{rangeAnchorHandle}, handle);
            if (range.has_value())
            {
                events::scene::SelectEntitiesCommand cmd;
                cmd.entities = std::move(*range);
                dispatcher.execute(cmd);
                return;
            }
            // Anchor not visible anymore — fall through to single select
        }

        if (io.KeyCtrl)
        {
            events::scene::SelectEntitiesCommand cmd;
            cmd.entities = selection::computeClickSelection(selectedHandles, handle,
                                                            {.ctrl = true});
            dispatcher.execute(cmd);
            rangeAnchorHandle = handle.id;
            return;
        }

        events::scene::SelectEntityCommand cmd;
        cmd.entity = handle;
        dispatcher.execute(cmd);
        rangeAnchorHandle = handle.id;
    }

    bool SceneHierarchyPanel::canRename(services::EntityHandle handle) const
    {
        events::scene::GetEntityQuery entityQuery;
        entityQuery.entity = handle;
        auto entityDataOpt = events::EventDispatcher::instance().query(entityQuery);
        if (!entityDataOpt.has_value())
        {
            return false;
        }

        // Structural / engine-generated entities aren't user-named: the scene Root
        // (no parent), and the Terrain node plus its auto-generated tile sub-entities.
        using CT = services::ComponentTypeId;
        return entityDataOpt->parent.has_value()
            && !entityDataOpt->hasComponent(CT::Terrain)
            && !entityDataOpt->hasComponent(CT::TerrainTile);
    }

    void SceneHierarchyPanel::beginRename(services::EntityHandle handle)
    {
        if (!canRename(handle))
        {
            return;
        }

        events::scene::GetEntityQuery entityQuery;
        entityQuery.entity = handle;
        auto entityDataOpt = events::EventDispatcher::instance().query(entityQuery);
        if (!entityDataOpt.has_value())
        {
            return;
        }

        renamingHandle = handle.id;
        renameFocusPending = true;
        snprintf(renameBuffer, sizeof(renameBuffer), "%s", entityDataOpt->name.c_str());
    }

    void SceneHierarchyPanel::drawRenameInput(services::EntityHandle handle)
    {
        ImGui::SameLine();
        if (renameFocusPending)
        {
            ImGui::SetKeyboardFocusHere();
            renameFocusPending = false;
        }
        ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 50.0f, 80.0f));
        bool committed = ImGui::InputText("##rename", renameBuffer, sizeof(renameBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        bool canceled = ImGui::IsKeyPressed(ImGuiKey_Escape);

        if (canceled)
        {
            renamingHandle = services::EntityHandle::INVALID_ID;
        }
        else if (committed || ImGui::IsItemDeactivated())
        {
            if (renameBuffer[0] != '\0')
            {
                events::scene::SetEntityNameCommand cmd;
                cmd.entity = handle;
                cmd.newName = renameBuffer;
                events::EventDispatcher::instance().execute(cmd);
            }
            renamingHandle = services::EntityHandle::INVALID_ID;
        }
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
        bool isRoot = !entityDataOpt.has_value() || !entityDataOpt->parent.has_value();

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

        visibleOrder.push_back(handle);

        // Panel-owned persistent open state
        bool expanded = expandedHandles.count(handle.id) > 0;
        ImGui::SetNextItemOpen(expanded, ImGuiCond_Always);

        ImGuiTreeNodeFlags flags = isSelected(handle) ? ImGuiTreeNodeFlags_Selected : 0;
        flags |= ImGuiTreeNodeFlags_OpenOnArrow;

        bool isRenaming = (renamingHandle == handle.id);

        // Entity icon
        const char* icon = entityDataOpt.has_value() ? getEntityIcon(*entityDataOpt) : ICON_FA_CIRCLE_DOT;
        char label[256];
        if (isRenaming)
        {
            snprintf(label, sizeof(label), "%s ", icon);
        }
        else
        {
            snprintf(label, sizeof(label), "%s  %s", icon, entityName.c_str());
        }

        bool nodeOpen = ImGui::TreeNodeEx((void*)handle.id, flags, "%s", label);

        if (ImGui::IsItemToggledOpen())
        {
            if (nodeOpen)
                expandedHandles.insert(handle.id);
            else
                expandedHandles.erase(handle.id);
        }

        if (isRenaming)
        {
            drawRenameInput(handle);
        }
        else
        {
            // Select entity when tree node is clicked
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                bool isSculptMode = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});
                if (!isSculptMode)
                {
                    handleSelectionClick(handle);
                }
            }

            // Double-click on the label starts inline rename
            if (!isRoot && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                beginRename(handle);
            }

            // Drag-drop must be right after TreeNodeEx (before SameLine buttons)
            bool isLocked = lockedEntities.count(handle.id) > 0;
            if (!isLocked && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("DND_SCENE_ENTITY", &handle, sizeof(services::EntityHandle));
                ImGui::Text("Move %s", entityName.c_str());
                ImGui::EndDragDropSource();
            }

            dragDropEntity(handle);

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
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::SmallButton(isLocked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN))
            {
                if (isLocked)
                    lockedEntities.erase(handle.id);
                else
                    lockedEntities.insert(handle.id);
            }
            ImGui::PopStyleColor();
        }

        if (nodeOpen)
        {
            if (entityDataOpt.has_value())
            {
                // While an entity drag is active, show thin drop zones between siblings
                // so the drag can reorder (insert) instead of reparenting
                bool dragActive = isEntityDragActive();
                const auto& children = entityDataOpt->children;
                for (size_t i = 0; i < children.size(); ++i)
                {
                    if (dragActive)
                    {
                        drawReorderDropZone(handle, i);
                    }
                    drawEntityNode(children[i]);
                }
                if (dragActive && !children.empty())
                {
                    drawReorderDropZone(handle, children.size());
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneHierarchyPanel::drawReorderDropZone(services::EntityHandle parent, size_t index)
    {
        ImGui::PushID(static_cast<int>(index));
        float width = std::max(ImGui::GetContentRegionAvail().x, 10.0f);
        ImGui::InvisibleButton("##reorderZone", ImVec2(width, 4.0f));

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SCENE_ENTITY"))
            {
                services::EntityHandle draggedHandle = *(services::EntityHandle*)payload->Data;
                if (draggedHandle.id != parent.id)
                {
                    events::scene::ReorderEntityCommand cmd;
                    cmd.entity = draggedHandle;
                    cmd.newParent = parent;
                    cmd.insertIndex = static_cast<int>(index);
                    events::EventDispatcher::instance().execute(cmd);
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
    }

    void SceneHierarchyPanel::drawSearchResults(const services::SceneHierarchyData& hierarchy)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        for (const auto& data : hierarchy.entities)
        {
            if (!data.parent.has_value())
            {
                continue; // skip the root
            }
            if (!matchesTypeFilter(data) || !matchesNameSearch(data.name))
            {
                continue;
            }

            visibleOrder.push_back(data.handle);

            ImGui::PushID(static_cast<int>(data.handle.id));

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (isSelected(data.handle))
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            char label[256];
            snprintf(label, sizeof(label), "%s  %s", getEntityIcon(data), data.name.c_str());
            ImGui::TreeNodeEx((void*)data.handle.id, flags, "%s", label);

            if (ImGui::IsItemClicked())
            {
                bool isSculptMode = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});
                if (!isSculptMode)
                {
                    handleSelectionClick(data.handle);
                }
            }

            ImGui::PopID();
        }
    }

    void SceneHierarchyPanel::drawContextMenu(const services::SceneHierarchyData& hierarchy)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (!ImGui::BeginPopupContextWindow())
        {
            return;
        }

        if (ImGui::MenuItem("Add New Entity"))
        {
            services::EntityHandle parentHandle = selectedHandle.isValid()
                                                      ? selectedHandle
                                                      : (hierarchy.entities.empty()
                                                             ? services::EntityHandle::invalid()
                                                             : hierarchy.entities[0].handle);

            events::scene::CreateEntityCommand cmd;
            cmd.name = "New Entity";
            cmd.parent = parentHandle.isValid() ? std::optional{parentHandle} : std::nullopt;
            dispatcher.execute(cmd);
        }

        auto topLevelSelection = collectTopLevelSelection();
        if (!topLevelSelection.empty())
        {
            int count = static_cast<int>(topLevelSelection.size());
            char menuLabel[64];

            ImGui::Separator();

            if (count == 1)
            {
                bool renamable = canRename(topLevelSelection.front());
                if (ImGui::MenuItem("Rename", "F2", false, renamable))
                {
                    beginRename(topLevelSelection.front());
                }
            }

            snprintf(menuLabel, sizeof(menuLabel), count > 1 ? "Duplicate (%d)" : "Duplicate", count);
            if (ImGui::MenuItem(menuLabel, "Ctrl+D"))
            {
                duplicateSelection();
            }

            snprintf(menuLabel, sizeof(menuLabel), count > 1 ? "Copy (%d)" : "Copy", count);
            if (ImGui::MenuItem(menuLabel, "Ctrl+C"))
            {
                copySelection();
            }
        }

        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !clipboardEntities.empty()))
        {
            pasteClipboard();
        }

        if (!topLevelSelection.empty())
        {
            int count = static_cast<int>(topLevelSelection.size());
            char menuLabel[64];
            snprintf(menuLabel, sizeof(menuLabel),
                     count > 1 ? "Remove Selected Entities (%d)" : "Remove Selected Entity", count);
            if (ImGui::MenuItem(menuLabel, "Del"))
            {
                deleteSelection();
            }
        }

        ImGui::EndPopup();
    }

    std::vector<services::EntityHandle> SceneHierarchyPanel::collectTopLevelSelection() const
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto rootHandle = dispatcher.query(events::scene::GetRootEntityQuery{});

        auto parentOf = [&dispatcher](services::EntityHandle handle)
            -> std::optional<services::EntityHandle>
        {
            events::scene::GetEntityQuery entityQuery;
            entityQuery.entity = handle;
            auto dataOpt = dispatcher.query(entityQuery);
            if (!dataOpt.has_value()) return std::nullopt;
            return dataOpt->parent;
        };
        return selection::collectTopLevel(selectedHandles, parentOf, rootHandle);
    }

    void SceneHierarchyPanel::duplicateSelection()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto topLevelSelection = collectTopLevelSelection();
        if (topLevelSelection.empty())
        {
            return;
        }

        events::undoredo::BeginBatchCommand batchCmd;
        batchCmd.description = "Duplicate Entities";
        dispatcher.execute(batchCmd);

        std::vector<services::EntityHandle> duplicated;
        duplicated.reserve(topLevelSelection.size());
        for (const auto& handle : topLevelSelection)
        {
            events::scene::DuplicateEntityCommand cmd;
            cmd.entity = handle;
            auto newHandle = dispatcher.execute(cmd);
            if (newHandle.isValid())
            {
                duplicated.push_back(newHandle);
            }
        }

        dispatcher.execute(events::undoredo::EndBatchCommand{});

        if (!duplicated.empty())
        {
            events::scene::SelectEntitiesCommand selectCmd;
            selectCmd.entities = std::move(duplicated);
            dispatcher.execute(selectCmd);
        }
    }

    void SceneHierarchyPanel::deleteSelection()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto topLevelSelection = collectTopLevelSelection();
        if (topLevelSelection.empty())
        {
            return;
        }

        events::undoredo::BeginBatchCommand batchCmd;
        batchCmd.description = "Delete Entities";
        dispatcher.execute(batchCmd);
        for (const auto& handle : topLevelSelection)
        {
            events::scene::DeleteEntityCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }
        dispatcher.execute(events::undoredo::EndBatchCommand{});
    }

    void SceneHierarchyPanel::copySelection()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto topLevelSelection = collectTopLevelSelection();
        if (topLevelSelection.empty())
        {
            return;
        }

        clipboardEntities.clear();
        for (const auto& handle : topLevelSelection)
        {
            events::scene::CopyEntityToJsonQuery query;
            query.entity = handle;
            auto jsonText = dispatcher.query(query);
            if (!jsonText.empty())
            {
                clipboardEntities.push_back(std::move(jsonText));
            }
        }
    }

    void SceneHierarchyPanel::pasteClipboard()
    {
        if (clipboardEntities.empty())
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // Paste as siblings of the primary selection (its parent); root when nothing selected
        std::optional<services::EntityHandle> pasteParent;
        if (selectedHandle.isValid())
        {
            events::scene::GetEntityQuery entityQuery;
            entityQuery.entity = selectedHandle;
            auto dataOpt = dispatcher.query(entityQuery);
            if (dataOpt.has_value() && dataOpt->parent.has_value())
            {
                pasteParent = dataOpt->parent;
            }
        }

        std::vector<services::EntityHandle> pasted;
        pasted.reserve(clipboardEntities.size());
        for (const auto& jsonText : clipboardEntities)
        {
            events::scene::InstantiateEntityFromJsonCommand cmd;
            cmd.jsonText = jsonText;
            cmd.parent = pasteParent;
            auto newHandle = dispatcher.execute(cmd);
            if (newHandle.has_value() && newHandle->isValid())
            {
                pasted.push_back(*newHandle);
            }
        }

        if (!pasted.empty())
        {
            if (pasteParent.has_value())
            {
                expandedHandles.insert(pasteParent->id);
            }
            events::scene::SelectEntitiesCommand selectCmd;
            selectCmd.entities = std::move(pasted);
            dispatcher.execute(selectCmd);
        }
    }

    void SceneHierarchyPanel::handleShortcuts()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
            renamingHandle != services::EntityHandle::INVALID_ID || ImGui::IsAnyItemActive())
        {
            return;
        }

        // VK-1490: rename/delete/copy/paste/duplicate act on the selection and
        // are edit-mode only, like the selection clicks above.
        if (events::EventDispatcher::instance().query(events::editor::IsPlayModeQuery{}))
        {
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && selectedHandle.isValid())
        {
            auto rootHandle = events::EventDispatcher::instance().query(events::scene::GetRootEntityQuery{});
            if (selectedHandle.id != rootHandle.id)
            {
                beginRename(selectedHandle);
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            deleteSelection();
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
        {
            copySelection();
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
        {
            pasteClipboard();
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
        {
            duplicateSelection();
        }
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

            if (ImGui::AcceptDragDropPayload(DND_CONTENT_BROWSER))
            {
                std::vector<std::string> dragPaths = DragDropManager::instance().getDragPaths();
                DragDropManager::instance().endDrag();

                auto& dispatcher = events::EventDispatcher::instance();

                for (const auto& path : dragPaths)
                {
                    std::string ext = std::filesystem::path(path).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    if (ext == ".vfmat" || ext == ".vfmatinstance")
                    {
                        events::material::HasMaterialComponentQuery hasMatQuery;
                        hasMatQuery.entity = handle;
                        if (!dispatcher.query(hasMatQuery))
                        {
                            events::material::AddMaterialComponentCommand addCmd;
                            addCmd.entity = handle;
                            dispatcher.execute(addCmd);
                        }

                        events::material::SetDefaultMaterialCommand matCmd;
                        matCmd.entity = handle;
                        matCmd.materialPath = path;
                        dispatcher.execute(matCmd);
                    }
                    else if (ext == ".vfanimator")
                    {
                        events::scene::GetMeshDataQuery meshQuery;
                        meshQuery.entity = handle;
                        auto meshOpt = dispatcher.query(meshQuery);
                        if (meshOpt.has_value())
                        {
                            events::scene::SetMeshDataCommand meshCmd;
                            meshCmd.entity = handle;
                            meshCmd.meshData = *meshOpt;
                            meshCmd.meshData.animatorRef = asset::AssetRef::fromPath(path);
                            dispatcher.execute(meshCmd);
                        }
                    }
                    else if (ext == ".mt")
                    {
                        events::scripting::AttachScriptCommand scriptCmd;
                        scriptCmd.entity = handle;
                        scriptCmd.data.scriptPath = path;
                        dispatcher.execute(scriptCmd);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    void SceneHierarchyPanel::expandToSelection(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Walk up the parent chain and expand all ancestors (without collapsing others)
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
