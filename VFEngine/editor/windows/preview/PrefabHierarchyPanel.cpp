#include "print/Log.hpp"
#include "PrefabHierarchyPanel.hpp"
#include "PrefabPreviewWindow.hpp"
#include "PrefabRigValidation.hpp" // prefabrigval::PartRefStatus
#include "imgui.h"
#include <IconsFontAwesome6.h> // eye / eye-slash hide toggle
#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp" // Create/Delete/Get/Reparent/Reorder/Name/SetEntityActive
#include "events/physics/SocketEvents.hpp"        // GetSocketAttachmentDataQuery
#include <filesystem>
#include <cstdio>
#include <cstring>

namespace windows
{
    services::EntityHandle PrefabHierarchyPanel::createChildEntity(services::EntityHandle parent)
    {
        if (!parent.isValid()) parent = ctx.sandboxRoot;
        if (!parent.isValid()) return services::EntityHandle::invalid();

        events::scene::CreateEntityCommand cmd;
        cmd.name = "Entity";
        cmd.parent = parent;
        const services::EntityHandle created = events::EventDispatcher::instance().execute(cmd);
        if (created.isValid())
        {
            selectEntity(created);
            ctx.dirty = true;
            w.sandboxController.rebuildRigFromSandbox();
        }
        return created;
    }

    void PrefabHierarchyPanel::requestDeleteEntity(services::EntityHandle entity)
    {
        // VK-1433 Phase 4d — stage the delete + open the confirm modal instead of deleting now. Never
        // the sandbox root. The actual DeleteEntityCommand runs in deleteEntity() on confirm.
        if (!entity.isValid() || entity == ctx.sandboxRoot) return;

        pendingDeleteEntity = entity;
        // Capture the name now (for the modal message) — the entity still exists at request time.
        events::scene::GetEntityQuery q;
        q.entity = entity;
        auto data = events::EventDispatcher::instance().query(q);
        pendingDeleteName = (data.has_value() && !data->name.empty()) ? data->name : "(unnamed)";
        openDeleteConfirmPopup = true; // consumed by drawDeleteConfirmPopup() this frame
    }

    bool PrefabHierarchyPanel::deleteEntity(services::EntityHandle entity)
    {
        // Never the sandbox root — that's the saved subtree's top (the prefab itself).
        if (!entity.isValid() || entity == ctx.sandboxRoot) return false;

        const bool wasSelected = (selectedEntity() == entity);
        events::scene::DeleteEntityCommand del;
        del.entity = entity;
        events::EventDispatcher::instance().execute(del);
        if (wasSelected) selectEntity(ctx.sandboxRoot);
        ctx.dirty = true;
        w.sandboxController.rebuildRigFromSandbox();
        return true;
    }

    void PrefabHierarchyPanel::drawDeleteConfirmPopup()
    {
        // VK-1433 Phase 4d — confirm-on-delete modal shared by all three delete affordances. Opened by
        // requestDeleteEntity (sets openDeleteConfirmPopup); Delete runs DeleteEntityCommand via
        // deleteEntity, Cancel is a no-op. The popup is defined at the window-root ID scope (called once
        // per frame from draw(), outside the hierarchy recursion).
        if (openDeleteConfirmPopup)
        {
            ImGui::OpenPopup("Delete Entity?##prefabDelete");
            openDeleteConfirmPopup = false;
        }

        // Center the modal over the window.
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Delete Entity?##prefabDelete", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete '%s' and its children?", pendingDeleteName.c_str());
            ImGui::TextDisabled("This removes the entity (and its descendants) from the prefab and "
                                "cannot be undone.");
            ImGui::Spacing();
            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                deleteEntity(pendingDeleteEntity);
                pendingDeleteEntity = services::EntityHandle::invalid();
                pendingDeleteName.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                pendingDeleteEntity = services::EntityHandle::invalid();
                pendingDeleteName.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::EndPopup();
        }
    }

    void PrefabHierarchyPanel::drawEntityTreePanel()
    {
        // VK-1433 Phase 4d — Hierarchy header toolbar: "+ Add" (child under selection/root) and a
        // trash "Delete" (enabled only for a non-root selection). Both share createChildEntity /
        // deleteEntity with the per-node context menu and the Delete-key shortcut.
        ImGui::TextDisabled("Hierarchy");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_PLUS " Add##addEntity"))
        {
            createChildEntity(selectedEntity().isValid() ? selectedEntity() : ctx.sandboxRoot);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add a child entity under the selection (or the root)");

        ImGui::SameLine();
        const services::EntityHandle sel = selectedEntity();
        const bool canDelete = sel.isValid() && sel != ctx.sandboxRoot;
        ImGui::BeginDisabled(!canDelete);
        if (ImGui::SmallButton(ICON_FA_TRASH " Delete##delEntity"))
        {
            requestDeleteEntity(sel); // confirm modal; actual delete on confirm
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(canDelete ? "Delete the selected entity (Del)"
                                        : "Select a non-root entity to delete (Del)");

        if (ctx.loadFailed || !ctx.prefabLoaded || !ctx.sandboxRoot.isValid())
        {
            ImGui::TextDisabled("No data");
            return;
        }
        // VK-1433 Phase 4b — CQRS-recursive tree over the live sandbox subtree with add/remove/
        // rename/reparent/reorder/hide. Drag a node onto another to reparent; onto a between-siblings
        // zone to reorder; double-click to rename; the eye toggles the saved Active state.
        drawEntityNode(ctx.sandboxRoot, 0);

        // VK-1433 Phase 4d — Delete-key shortcut: when this hierarchy panel (or any of its children) is
        // focused and no inline rename / text field is active, Del removes the selected non-root entity
        // via the same deleteEntity path. IsWindowFocused(RootAndChildWindows) covers the InfoPanel
        // child the tree lives in; the rename guard avoids stealing Del from the rename InputText.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !renamingEntity.isValid() && !ImGui::IsAnyItemActive() &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            requestDeleteEntity(selectedEntity()); // confirm modal; actual delete on confirm
        }
    }

    namespace
    {
        constexpr const char* kPrefabSceneEntityPayload = "DND_SCENE_ENTITY"; // shared with SceneHierarchyPanel

        // Is `ancestor` an ancestor of (or equal to) `node` within the sandbox subtree? Used to guard
        // a reparent/reorder that would create a cycle (drop a node onto its own descendant). CQRS-only.
        bool isAncestorOrSelf(services::EntityHandle ancestor, services::EntityHandle node)
        {
            services::EntityHandle cur = node;
            while (cur.isValid())
            {
                if (cur == ancestor) return true;
                events::scene::GetEntityQuery q;
                q.entity = cur;
                auto data = events::EventDispatcher::instance().query(q);
                if (!data.has_value() || !data->parent.has_value()) break;
                cur = *data->parent;
            }
            return false;
        }

        // Is an entity-hierarchy drag currently in flight? (mirror SceneHierarchyPanel) — gates the
        // between-siblings reorder drop zones so they only appear mid-drag.
        bool isPrefabEntityDragActive()
        {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            return payload != nullptr && payload->IsDataType(kPrefabSceneEntityPayload);
        }
    }

    void PrefabHierarchyPanel::drawReorderDropZone(services::EntityHandle parent, size_t index)
    {
        ImGui::PushID(static_cast<int>(index));
        const float width = std::max(ImGui::GetContentRegionAvail().x, 10.0f);
        ImGui::InvisibleButton("##reorderZone", ImVec2(width, 4.0f));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPrefabSceneEntityPayload))
            {
                const services::EntityHandle dragged =
                    *static_cast<const services::EntityHandle*>(payload->Data);
                // Guard: a node can't be reordered under itself or any of its own descendants.
                if (dragged.isValid() && dragged != parent && !isAncestorOrSelf(dragged, parent))
                {
                    events::scene::ReorderEntityCommand cmd;
                    cmd.entity = dragged;
                    cmd.newParent = parent;
                    cmd.insertIndex = static_cast<int>(index);
                    events::EventDispatcher::instance().execute(cmd);
                    ctx.dirty = true;
                    w.sandboxController.rebuildRigFromSandbox();
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
    }

    std::string PrefabHierarchyPanel::missingRefTooltip(int part) const
    {
        if (part < 0 || part >= static_cast<int>(ctx.partRefStatuses.size())) return {};
        const prefabrigval::PartRefStatus& s = ctx.partRefStatuses[part];
        if (!s.anyMissing()) return {};

        const services::PrefabRigPartDTO& p = ctx.rigDesc.parts[part];
        std::string out;
        auto addLine = [&out](const std::string& label, const std::string& path)
        {
            if (!out.empty()) out += "\n";
            out += label + " not found: " + path;
        };
        if (s.meshMissing) addLine("Mesh", p.meshPath);
        if (s.animatorMissing) addLine("Animator", p.animatorPath);
        if (s.retargetMissing) addLine("Retarget", p.retargetPath);
        if (s.defaultMaterialMissing) addLine("Material", p.defaultMaterialPath);
        for (const auto& submesh : s.missingSubMeshMaterials)
        {
            auto it = p.subMeshMaterials.find(submesh);
            addLine("Submesh material '" + submesh + "'", it != p.subMeshMaterials.end() ? it->second : "");
        }
        return out;
    }

    void PrefabHierarchyPanel::drawEntityNode(services::EntityHandle entity, int depth)
    {
        if (!entity.isValid()) return;

        events::scene::GetEntityQuery q;
        q.entity = entity;
        auto data = events::EventDispatcher::instance().query(q);
        if (!data.has_value()) return;

        ImGui::PushID(static_cast<int>(entity.id));

        // A mesh-bearing entity maps to a rig part (partEntities is the live builder's parallel
        // source-entity vector); use it for the [skel]/[static] badge and the broken-ref tint.
        const int thisPart = partForEntity(entity);
        const std::string tooltip = (thisPart >= 0) ? missingRefTooltip(thisPart) : std::string();
        const bool missing = !tooltip.empty();

        const bool isSelected = (selectedEntity() == entity);
        const bool hasChildren = !data->children.empty();
        const bool isRoot = (entity == ctx.sandboxRoot);
        const bool isRenaming = (renamingEntity == entity);
        const bool isHidden = !isRoot && !data->isActive;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen
                                   | ImGuiTreeNodeFlags_SpanAvailWidth
                                   | ImGuiTreeNodeFlags_AllowOverlap; // let the right-aligned eye button take its own clicks
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

        std::string label = data->name.empty() ? "(unnamed)" : data->name;
        if (thisPart >= 0) label += w.authoringController.partIsSkeletal(thisPart) ? " [skel]" : " [static]";
        if (isHidden) label += " (hidden)";

        // A hidden node (inactive entity) is dimmed; a broken-ref node is tinted red.
        bool pushedColor = false;
        if (missing) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f)); pushedColor = true; }
        else if (isHidden) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f)); pushedColor = true; }

        const bool nodeOpen = ImGui::TreeNodeEx("##node", flags, "%s%s",
                                                isRenaming ? "" : label.c_str(),
                                                (missing && !isRenaming) ? "  (!)" : "");
        if (pushedColor) ImGui::PopStyleColor();
        if (missing && !isRenaming && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip.c_str());

        if (!isRenaming)
        {
            // Select on click; double-click begins an inline rename (the root included — it's the
            // saved subtree's own node).
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                selectEntity(entity);
                if (thisPart >= 0) w.authoringController.selectPart(thisPart);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                renamingEntity = entity;
                renameFocusPending = true;
                snprintf(renameBuf, sizeof(renameBuf), "%s", data->name.c_str());
            }

            // Drag source (right after TreeNodeEx, before any SameLine widgets). The sandbox root is
            // not draggable (it has no parent to reparent under).
            if (!isRoot && ImGui::BeginDragDropSource())
            {
                services::EntityHandle payload = entity;
                ImGui::SetDragDropPayload(kPrefabSceneEntityPayload, &payload, sizeof(services::EntityHandle));
                ImGui::Text("Move %s", data->name.empty() ? "(unnamed)" : data->name.c_str());
                ImGui::EndDragDropSource();
            }

            // Drop target: reparent the dragged node under this one (guarded against cycles).
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPrefabSceneEntityPayload))
                {
                    const services::EntityHandle dragged =
                        *static_cast<const services::EntityHandle*>(payload->Data);
                    if (dragged.isValid() && dragged != entity && !isAncestorOrSelf(dragged, entity))
                    {
                        events::scene::ReparentEntityCommand cmd;
                        cmd.entity = dragged;
                        cmd.newParent = entity; // handler also enforces its own cycle-check
                        events::EventDispatcher::instance().execute(cmd);
                        ctx.dirty = true;
                        w.sandboxController.rebuildRigFromSandbox();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // VK-1433 Phase 4d — eye toggle, right-aligned. Mirrors UILayerBuilderWindow:
            // non-root nodes flip their authored Active state, so the preview updates immediately
            // and SavePrefab persists the visibility.
            if (!isRoot)
            {
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, isHidden ? ImVec4(0.95f, 0.6f, 0.2f, 1.0f)
                                                              : ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
                if (ImGui::SmallButton(isHidden ? ICON_FA_EYE_SLASH "##hide" : ICON_FA_EYE "##hide"))
                {
                    events::scene::SetEntityActiveCommand cmd;
                    cmd.entity = entity;
                    cmd.isActive = !data->isActive;
                    events::EventDispatcher::instance().execute(cmd);
                    ctx.dirty = true;
                    w.sandboxController.rebuildRigFromSandbox();
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(isHidden ? "Show" : "Hide");
            }

            // Context menu: add child / delete (never the sandbox root — that's the saved subtree's
            // top). Shares createChildEntity / deleteEntity with the header toolbar + Delete key.
            if (!isRoot && ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Add Child"))
                {
                    createChildEntity(entity);
                }
                if (ImGui::MenuItem("Delete"))
                {
                    // Phase 4d — defer to the confirm modal. The entity still exists this frame (the
                    // actual delete runs on confirm), so the old abort-recursion early-return is gone.
                    requestDeleteEntity(entity);
                }
                ImGui::EndPopup();
            }
        }
        else
        {
            // Inline rename editor (replaces the node's interactions for this frame).
            ImGui::SameLine();
            if (renameFocusPending)
            {
                ImGui::SetKeyboardFocusHere();
                renameFocusPending = false;
            }
            ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 10.0f, 80.0f));
            const bool committed = ImGui::InputText("##rename", renameBuf, sizeof(renameBuf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue
                                                        | ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                renamingEntity = services::EntityHandle::invalid();
            }
            else if (committed || ImGui::IsItemDeactivated())
            {
                if (renameBuf[0] != '\0' && renameBuf != data->name)
                {
                    // O2: a socket attachment resolves its parent BY NAME (subtree-scoped), so
                    // renaming an entity that is some child's attach-parent drops that child's link to
                    // root on the next re-derive. WARN (non-destructive, no cascade) and proceed.
                    if (renameWouldOrphanAttachment(data->name))
                    {
                        vfLogWarning("Prefab preview: renaming '{}' breaks a socket attachment that "
                                     "resolves to it by name; the attached child will fall back to "
                                     "the root until you re-point its socket parent.", data->name);
                    }
                    events::scene::SetEntityNameCommand cmd;
                    cmd.entity = entity;
                    cmd.newName = renameBuf;
                    events::EventDispatcher::instance().execute(cmd);
                    ctx.dirty = true;
                    w.sandboxController.rebuildRigFromSandbox();
                }
                renamingEntity = services::EntityHandle::invalid();
            }
        }

        if (nodeOpen && hasChildren)
        {
            const bool dragActive = isPrefabEntityDragActive();
            const auto& children = data->children;
            for (size_t i = 0; i < children.size(); ++i)
            {
                if (dragActive) drawReorderDropZone(entity, i);
                drawEntityNode(children[i], depth + 1);
            }
            if (dragActive) drawReorderDropZone(entity, children.size());
            ImGui::TreePop();
        }
        else if (nodeOpen)
        {
            // Leaf nodes still TreePush unless NoTreePushOnOpen; we did NOT set that flag (so DnD +
            // SameLine widgets attach correctly), so balance the push here.
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    bool PrefabHierarchyPanel::renameWouldOrphanAttachment(const std::string& oldName) const
    {
        if (oldName.empty()) return false;
        // Scan the live parts' source entities for a socket attachment whose parentEntityName equals
        // the about-to-be-renamed name. If one exists, that link resolves by name and will drop.
        for (const services::EntityHandle& part : ctx.partEntities)
        {
            events::socket::GetSocketAttachmentDataQuery sq;
            sq.entity = part;
            auto attach = events::EventDispatcher::instance().query(sq);
            if (attach.has_value() && attach->parentEntityName == oldName)
                return true;
        }
        return false;
    }

    void PrefabHierarchyPanel::drawEntityInspector()
    {
        const services::EntityHandle sel = selectedEntity();
        if (!sel.isValid())
        {
            ImGui::TextDisabled("Select an entity in the hierarchy.");
            return;
        }

        // Embedded shared inspector: full per-component edit + Remove + Add Component (O1). A
        // structural component change (add/remove) shifts the structure signature, which we detect
        // on the next frame to re-derive the rig.
        entityInspector.drawComponentSection(sel);
    }

    // ----------------------------------------------------------------------
    // VK-1433 Phase 4 — selection link (hierarchy <-> part-indexed panels)
    // ----------------------------------------------------------------------
    services::EntityHandle PrefabHierarchyPanel::selectedEntity() const
    {
        // WINDOW-LOCAL selection (see header): the sandbox subtree's selection must NOT touch the
        // global scene selection, or selecting a sandbox entity here would also re-select it in the
        // main Scene Hierarchy / Details panels (and a main-scene selection would blank this inspector).
        return ctx.selectedSandboxEntity;
    }

    void PrefabHierarchyPanel::selectEntity(services::EntityHandle entity)
    {
        // Set the window-local selection only. Callers additionally drive the part-indexed authoring
        // panels via partForEntity()/selectPart(); no global SelectEntityCommand is issued.
        ctx.selectedSandboxEntity = entity;
    }

    int PrefabHierarchyPanel::partForEntity(services::EntityHandle entity) const
    {
        if (!entity.isValid()) return -1;
        for (size_t i = 0; i < ctx.partEntities.size(); ++i)
        {
            if (ctx.partEntities[i] == entity)
                return static_cast<int>(i);
        }
        return -1;
    }

    void PrefabHierarchyPanel::onSandboxClosed()
    {
        // VK-1443 — inline-rename state reset, formerly inline in closeSandbox().
        renamingEntity = services::EntityHandle::invalid();
        renameFocusPending = false;
    }
}
