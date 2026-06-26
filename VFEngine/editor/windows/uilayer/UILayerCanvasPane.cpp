#include "UILayerCanvasPane.hpp"
#include "UILayerBuilderWindow.hpp"

#include "events/EventDispatcher.hpp"
#include "events/ui/UICanvasRectImageEvents.hpp"
#include "events/render/UILayerPreviewEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp" // DeleteEntityCommand (canvas context menu)
#include "UILayerCanvasHandles.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <string>

namespace windows::uilayer
{
    namespace
    {
        using Dispatcher = events::EventDispatcher;
    }

    void UILayerCanvasPane::draw()
    {
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        glm::vec2 regionOrigin(cursor.x, cursor.y);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        glm::vec2 regionSize(avail.x, avail.y);
        if (regionSize.x <= 0.0f || regionSize.y <= 0.0f) return;

        drawCanvasImageAndHandles(regionOrigin, regionSize);
    }

    void UILayerCanvasPane::drawCanvasImageAndHandles(glm::vec2 regionOrigin, glm::vec2 regionSize)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Backdrop (checker-ish dark fill) so the letterboxed area is visible.
        dl->AddRectFilled(ImVec2(regionOrigin.x, regionOrigin.y),
                          ImVec2(regionOrigin.x + regionSize.x, regionOrigin.y + regionSize.y),
                          IM_COL32(28, 28, 32, 255));

        glm::vec2 refExtent = w.canvasReferenceExtent();
        uilayer::LetterboxMapping map =
            uilayer::makeLetterbox(regionOrigin, regionSize, refExtent, w.zoom, w.panRef);

        // The offscreen image, drawn letterboxed.
        void* tex = w.renderPreview();
        ImVec2 imgTL(map.originScreen.x, map.originScreen.y);
        ImVec2 imgBR(map.originScreen.x + map.imageSizeScreen.x,
                     map.originScreen.y + map.imageSizeScreen.y);
        if (tex)
        {
            dl->AddImage(tex, imgTL, imgBR);
        }
        else
        {
            dl->AddRectFilled(imgTL, imgBR, IM_COL32(15, 15, 18, 255));
        }
        // Canvas border.
        dl->AddRect(imgTL, imgBR, IM_COL32(90, 90, 100, 255));

        // An invisible button over the content region captures mouse input for the canvas.
        ImGui::SetCursorScreenPos(ImVec2(regionOrigin.x, regionOrigin.y));
        ImGui::InvisibleButton("##canvas", ImVec2(regionSize.x, regionSize.y),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

        handleCanvasInput(map);

        // Selection handles overlay (drawn after input so it reflects the live rect).
        services::EntityHandle sel = w.selectedEntity();
        if (sel.isValid())
        {
            if (auto rect = w.resolvedRectOf(sel))
            {
                drawHandleOverlay(dl, map, *rect);
            }
        }
    }

    void UILayerCanvasPane::drawHandleOverlay(ImDrawList* dl, const uilayer::LetterboxMapping& map,
                                              const uilayer::RefRect& rect)
    {
        glm::vec2 tl = map.refToScreen(glm::vec2(rect.x, rect.y));
        glm::vec2 br = map.refToScreen(glm::vec2(rect.right(), rect.bottom()));

        // When the element's position is driven by a parent Layout Group, the move-drag is
        // disabled (see handleCanvasInput) — signal it with a muted-blue outline + a badge.
        const bool layoutControlled = w.isLayoutControlled(w.selectedEntity());
        const ImU32 outlineCol = layoutControlled ? IM_COL32(110, 170, 255, 255)
                                                   : IM_COL32(255, 180, 40, 255);

        // Selection outline.
        dl->AddRect(ImVec2(tl.x, tl.y), ImVec2(br.x, br.y), outlineCol, 0.0f, 0, 1.5f);
        if (layoutControlled)
            dl->AddText(ImVec2(tl.x + 3.0f, tl.y - 15.0f), outlineCol, "Layout-controlled");

        // 8 resize handles (resize / re-anchor still apply even when layout-controlled).
        const float hs = 4.0f; // half-size in screen px
        auto positions = uilayer::handlePositions(rect);
        for (const auto& p : positions)
        {
            glm::vec2 s = map.refToScreen(p);
            dl->AddRectFilled(ImVec2(s.x - hs, s.y - hs), ImVec2(s.x + hs, s.y + hs),
                              IM_COL32(255, 180, 40, 255));
            dl->AddRect(ImVec2(s.x - hs, s.y - hs), ImVec2(s.x + hs, s.y + hs), IM_COL32(20, 20, 20, 255));
        }

        // Anchor markers (the element's anchor rectangle on the canvas).
        if (w.showAnchors)
        {
            if (auto data = w.rectDataOf(w.selectedEntity()))
            {
                glm::vec2 ext = w.canvasReferenceExtent();
                float aL = data->anchorMin.x * ext.x;
                float aR = data->anchorMax.x * ext.x;
                float aT = (1.0f - data->anchorMax.y) * ext.y;
                float aB = (1.0f - data->anchorMin.y) * ext.y;
                glm::vec2 atl = map.refToScreen(glm::vec2(aL, aT));
                glm::vec2 abr = map.refToScreen(glm::vec2(aR, aB));
                dl->AddRect(ImVec2(atl.x, atl.y), ImVec2(abr.x, abr.y), IM_COL32(80, 160, 255, 200), 0.0f,
                            0, 1.0f);
            }
        }

        // Pivot marker.
        if (w.showPivots)
        {
            if (auto data = w.rectDataOf(w.selectedEntity()))
            {
                float px = rect.x + data->pivot.x * rect.w;
                float py = rect.y + (1.0f - data->pivot.y) * rect.h; // pivot.y is bottom-up
                glm::vec2 s = map.refToScreen(glm::vec2(px, py));
                dl->AddCircle(ImVec2(s.x, s.y), 5.0f, IM_COL32(40, 255, 120, 255), 12, 1.5f);
                dl->AddLine(ImVec2(s.x - 7, s.y), ImVec2(s.x + 7, s.y), IM_COL32(40, 255, 120, 255));
                dl->AddLine(ImVec2(s.x, s.y - 7), ImVec2(s.x, s.y + 7), IM_COL32(40, 255, 120, 255));
            }
        }
    }

    void UILayerCanvasPane::handleCanvasInput(const uilayer::LetterboxMapping& map)
    {
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        ImGuiIO& io = ImGui::GetIO();
        glm::vec2 mouseScreen(io.MousePos.x, io.MousePos.y);
        glm::vec2 mouseRef = map.screenToRef(mouseScreen);

        // Middle-drag pans the canvas.
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            glm::vec2 deltaScreen(io.MouseDelta.x, io.MouseDelta.y);
            if (map.scale > 0.0f) w.panRef += deltaScreen / map.scale;
        }

        // Left mouse: select / start a handle drag.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            services::EntityHandle sel = w.selectedEntity();

            // Resize/re-anchor handles of the CURRENT selection take priority, so you can grab a
            // corner/edge even when it overlaps a child. The BODY hit is deliberately NOT treated
            // as a grab here: it must fall through to picking so a child *inside* the selected
            // parent stays selectable (clicking the parent's interior selects the top-most element
            // under the cursor instead of re-grabbing the parent). [VK-1442 fix]
            uilayer::HandleKind handle = uilayer::HandleKind::None;
            if (sel.isValid())
            {
                if (auto rect = w.resolvedRectOf(sel))
                {
                    float grabHalfRef = (map.scale > 0.0f) ? (6.0f / map.scale) : 6.0f;
                    uilayer::HandleKind h = uilayer::hitTestHandle(*rect, mouseRef, grabHalfRef);
                    if (h != uilayer::HandleKind::None && h != uilayer::HandleKind::Body)
                    {
                        handle = h; // a real resize/re-anchor handle of the selection
                        if (auto before = w.rectDataOf(sel))
                        {
                            w.dragging = true;
                            w.activeHandle = handle;
                            w.dragEntity = sel;
                            w.dragBefore = *before;
                            w.dragStartRect = *rect;
                            w.dragStartRefMouse = mouseRef;
                        }
                    }
                }
            }

            // Body / empty: pick the top-most element under the cursor and select it. If the click
            // landed on the ALREADY-selected element (not a re-selection) and it has a movable rect,
            // arm a move-drag so a click-drag relocates it — preserving the prior move behavior
            // without swallowing clicks on nested children.
            if (handle == uilayer::HandleKind::None)
            {
                services::events::uilayerpreview::PickUILayerElementAtQuery pick;
                pick.instanceId = w.instanceId();
                pick.refPx = mouseRef;
                auto hit = Dispatcher::instance().query(pick);
                services::EntityHandle target = hit.isValid() ? hit : w.contentRoot;

                const bool sameSelection = sel.isValid() && (target == sel);
                w.selectEntity(target);

                if (sameSelection && hit.isValid() && !w.isLayoutControlled(target))
                {
                    if (auto before = w.rectDataOf(target))
                    {
                        if (auto rect = w.resolvedRectOf(target))
                        {
                            w.dragging = true;
                            w.activeHandle = uilayer::HandleKind::Body;
                            w.dragEntity = target;
                            w.dragBefore = *before;
                            w.dragStartRect = *rect;
                            w.dragStartRefMouse = mouseRef;
                        }
                    }
                }
            }
        }

        // Right mouse: pick the element under the cursor so the context menu (below, in draw())
        // targets it. The menu itself is opened by BeginPopupContextItem on the canvas button.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            services::events::uilayerpreview::PickUILayerElementAtQuery pick;
            pick.instanceId = w.instanceId();
            pick.refPx = mouseRef;
            auto hit = Dispatcher::instance().query(pick);
            if (hit.isValid()) w.selectEntity(hit);
        }

        // Dragging a handle: dispatch live SetUIRectDataCommand (no undo push mid-drag).
        if (w.dragging && w.dragEntity.isValid())
        {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                glm::vec2 deltaRef = mouseRef - w.dragStartRefMouse;
                uilayer::RefRect target = uilayer::applyHandleDrag(w.dragStartRect, w.activeHandle, deltaRef);

                glm::vec2 ext = w.canvasReferenceExtent();
                services::UIRectData solved =
                    uilayer::solveRectData(w.dragBefore, target, ext.x, ext.y);

                events::ui::SetUIRectDataCommand cmd;
                cmd.entity = w.dragEntity;
                cmd.rectData = solved;
                Dispatcher::instance().execute(cmd);
                w.rebuildPreview();
            }

            // Mouse released: coalesce the whole drag into one undo entry.
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                if (auto after = w.rectDataOf(w.dragEntity))
                {
                    const char* verb = (w.activeHandle == uilayer::HandleKind::Body) ? "Move" : "Resize";
                    w.pushRectEditUndo(w.dragEntity, w.dragBefore, *after, std::string(verb) + " UI element");
                }
                w.dragging = false;
                w.activeHandle = uilayer::HandleKind::None;
                w.dragEntity = services::EntityHandle::invalid();
            }
        }

        // Canvas context menu (bound to the invisible canvas button, the current last item):
        // right-click opens it; the right-click above already selected the element under the cursor.
        if (ImGui::BeginPopupContextItem("##uiCanvasContext"))
        {
            services::EntityHandle sel = w.selectedEntity();
            const bool canDelete = sel.isValid() && sel != w.contentRoot && sel != w.canvasRoot;
            if (!canDelete) ImGui::BeginDisabled();
            if (ImGui::MenuItem("Delete"))
            {
                events::scene::DeleteEntityCommand del;
                del.entity = sel;
                Dispatcher::instance().execute(del);
                w.selectEntity(w.contentRoot);
                w.dirty = true;
                w.rebuildPreview();
            }
            if (!canDelete) ImGui::EndDisabled();
            ImGui::EndPopup();
        }
    }
}
