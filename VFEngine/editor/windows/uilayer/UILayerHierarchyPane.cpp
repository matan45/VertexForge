#include "UILayerHierarchyPane.hpp"
#include "UILayerBuilderWindow.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cstdio>

namespace windows::uilayer
{
    namespace
    {
        using Dispatcher = events::EventDispatcher;
    }

    void UILayerHierarchyPane::draw()
    {
        ImGui::SeparatorText("Hierarchy");
        if (!w.hasLayer()) return;

        // Reveal: when the selection changes (canvas-click or tree-click), expand the selected
        // entity's ancestors and request a one-shot scroll-into-view for its node.
        services::EntityHandle sel = w.selectedEntity();
        if (sel.isValid() && sel != w.lastRevealSel)
        {
            w.lastRevealSel = sel;
            w.revealScroll = true;

            // Walk up the parent chain, expanding each ancestor (without collapsing others).
            services::EntityHandle current = sel;
            while (current.isValid())
            {
                events::scene::GetEntityQuery q;
                q.entity = current;
                auto data = Dispatcher::instance().query(q);
                if (!data.has_value() || !data->parent.has_value() || !data->parent->isValid())
                    break;
                w.expandedNodes.insert(data->parent->id);
                current = *data->parent;
            }
        }
        else if (!sel.isValid())
        {
            w.lastRevealSel = services::EntityHandle::invalid();
        }

        if (ImGui::BeginChild("HierTree", ImVec2(0, 0)))
        {
            drawNode(w.contentRoot, 0);

            // Empty space below the tree is a top-level drop zone: dropping here reparents the
            // dragged entity directly under contentRoot ("outside / move to top level").
            ImVec2 remaining = ImGui::GetContentRegionAvail();
            if (remaining.y > 0.0f)
            {
                ImGui::Dummy(ImVec2(std::max(remaining.x, 1.0f), remaining.y));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kDragPayload))
                    {
                        services::EntityHandle dragged =
                            *static_cast<const services::EntityHandle*>(p->Data);
                        if (dragged.isValid() && w.contentRoot.isValid() && dragged.id != w.contentRoot.id)
                        {
                            events::scene::ReparentEntityCommand reparent;
                            reparent.entity = dragged;
                            reparent.newParent = w.contentRoot;
                            Dispatcher::instance().execute(reparent);
                            w.dirty = true;
                            w.rebuildPreview();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
        }
        ImGui::EndChild();
    }

    void UILayerHierarchyPane::drawNode(services::EntityHandle entity, int depth)
    {
        if (!entity.isValid()) return;

        events::scene::GetEntityQuery q;
        q.entity = entity;
        auto data = Dispatcher::instance().query(q);
        if (!data.has_value()) return;

        const bool isSelected = (w.selectedEntity() == entity);
        const bool hasChildren = !data->children.empty();
        const bool isRenaming = (w.renamingEntity == entity);

        // Default-open on first sight (preserves the original DefaultOpen UX); thereafter the
        // expandedNodes set is authoritative and tracks user collapses + reveal expansions.
        if (w.seenNodes.insert(entity.id).second)
        {
            w.expandedNodes.insert(entity.id);
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_AllowOverlap; // let the right-aligned eye button take its own clicks
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

        ImGui::PushID(static_cast<int>(entity.id));
        ImGui::SetNextItemOpen(w.expandedNodes.count(entity.id) > 0, ImGuiCond_Always);
        // Hidden (inactive) content nodes draw dimmed. The canvas root is held inactive for
        // isolation (not by the user), so it's never treated as hidden.
        const bool hidden = !data->isActive && entity != w.canvasRoot;
        if (hidden)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        bool open = ImGui::TreeNodeEx(data->name.empty() ? "(unnamed)" : data->name.c_str(), flags);
        if (hidden)
            ImGui::PopStyleColor();

        // Keep the expanded set in sync with user arrow toggles.
        if (ImGui::IsItemToggledOpen())
        {
            if (open) w.expandedNodes.insert(entity.id);
            else      w.expandedNodes.erase(entity.id);
        }

        // Scroll the freshly-revealed selection into view (one-shot).
        if (isSelected && w.revealScroll)
        {
            ImGui::SetScrollHereY(0.5f);
            w.revealScroll = false;
        }

        if (!isRenaming)
        {
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                w.selectEntity(entity);
            }

            // Double-click begins an inline rename (the content root is the layer top — still
            // renameable, since it's the saved subtree's own node).
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                w.renamingEntity = entity;
                w.renameFocusPending = true;
                snprintf(w.renameBuf, sizeof(w.renameBuf), "%s", data->name.c_str());
            }

            // Drag source: this node can be reparented onto another. Must come right after the
            // tree node (before any SameLine widgets).
            if (ImGui::BeginDragDropSource())
            {
                services::EntityHandle payload = entity;
                ImGui::SetDragDropPayload(kDragPayload, &payload, sizeof(services::EntityHandle));
                ImGui::Text("Move %s", data->name.empty() ? "(unnamed)" : data->name.c_str());
                ImGui::EndDragDropSource();
            }
        }

        // Drop target: reparent the dragged entity under this node.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kDragPayload))
            {
                services::EntityHandle dragged = *static_cast<const services::EntityHandle*>(p->Data);
                if (dragged.isValid() && dragged.id != entity.id)
                {
                    events::scene::ReparentEntityCommand reparent;
                    reparent.entity = dragged;
                    reparent.newParent = entity; // handler enforces the ancestor cycle-check
                    Dispatcher::instance().execute(reparent);
                    w.dirty = true;
                    w.rebuildPreview();
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Visibility toggle (eye icon), right-aligned. Flips the entity's isActive so the element
        // shows/hides LIVE in the preview (descendants honor it via isEffectivelyActiveWithin).
        // Skip the canvas root — its inactive state is an isolation artifact, not user visibility.
        if (!isRenaming && entity != w.canvasRoot)
        {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            if (ImGui::SmallButton(data->isActive ? ICON_FA_EYE : ICON_FA_EYE_SLASH))
            {
                events::scene::SetEntityActiveCommand cmd;
                cmd.entity = entity;
                cmd.isActive = !data->isActive;
                Dispatcher::instance().execute(cmd);
                w.dirty = true;
                w.rebuildPreview();
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(data->isActive ? "Hide" : "Show");
        }

        // Inline rename editor (replaces the node's normal interactions for this frame).
        if (isRenaming)
        {
            ImGui::SameLine();
            if (w.renameFocusPending)
            {
                ImGui::SetKeyboardFocusHere();
                w.renameFocusPending = false;
            }
            ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 10.0f, 80.0f));
            bool committed = ImGui::InputText("##rename", w.renameBuf, sizeof(w.renameBuf),
                                              ImGuiInputTextFlags_EnterReturnsTrue
                                                  | ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                w.renamingEntity = services::EntityHandle::invalid();
            }
            else if (committed || ImGui::IsItemDeactivated())
            {
                if (w.renameBuf[0] != '\0')
                {
                    events::scene::SetEntityNameCommand cmd;
                    cmd.entity = entity;
                    cmd.newName = w.renameBuf;
                    Dispatcher::instance().execute(cmd);
                    w.dirty = true;
                }
                w.renamingEntity = services::EntityHandle::invalid();
            }
        }

        // Delete (not the content root — that's the layer's top).
        if (entity != w.contentRoot && ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Delete"))
            {
                events::scene::DeleteEntityCommand del;
                del.entity = entity;
                Dispatcher::instance().execute(del);
                if (isSelected) w.selectEntity(w.contentRoot);
                w.dirty = true;
                w.rebuildPreview();
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            for (auto child : data->children)
            {
                drawNode(child, depth + 1);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}
