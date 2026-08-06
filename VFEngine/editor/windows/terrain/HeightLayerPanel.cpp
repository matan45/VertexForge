#include "HeightLayerPanel.hpp"

#include "data/HeightLayerStackView.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/SplineTerrainEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace
{
    // Drag payload id. Distinct from the scene/prefab entity payloads so a row can never be
    // dropped into a hierarchy panel, or an entity into this list.
    constexpr const char* kLayerDragPayload = "DND_TERRAIN_HEIGHT_LAYER";

    [[nodiscard]] bool isLayerDragActive()
    {
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        return payload != nullptr && payload->IsDataType(kLayerDragPayload);
    }
}

namespace windows
{
    HeightLayerPanel::~HeightLayerPanel()
    {
        if (stackToken.isValid())
            events::EventDispatcher::instance().unsubscribe(stackToken);
    }

    void HeightLayerPanel::subscribe()
    {
        if (subscribed)
            return;

        // The notification carries no payload by design: re-running the query is the only way to
        // stay the single source of truth. So this sets a flag and NOTHING ELSE — publish()
        // dispatches synchronously from inside the mutating handler, and editing the stack from
        // here would reenter an operation that has not finished.
        stackToken = events::EventDispatcher::instance()
                         .subscribe<events::splineTerrain::HeightLayerStackChangedNotification>(
                             [this](const events::splineTerrain::HeightLayerStackChangedNotification&)
                             { stackDirty = true; });

        subscribed = true;
    }

    void HeightLayerPanel::refresh()
    {
        stack = events::EventDispatcher::instance().query(
            events::splineTerrain::GetHeightLayerStackQuery{});
        stackDirty = false;

        // Drop a selection whose layer is gone (deleted from elsewhere, or the terrain changed).
        const bool stillThere =
            std::any_of(stack.begin(), stack.end(),
                        [this](const services::HeightLayerInfo& info)
                        { return info.id == selectedId; });
        if (!stillThere)
            selectedId = 0;
    }

    std::string HeightLayerPanel::displayName(const services::HeightLayerInfo& info)
    {
        if (!info.name.empty())
            return info.name;

        // A layer applied from a spline with no road name reaches the service with an empty label;
        // the id is at least stable and unique.
        return "Layer " + std::to_string(info.id);
    }

    void HeightLayerPanel::drawInsertZone(size_t zone)
    {
        // Emitted only while one of OUR rows is being dragged. A permanent 4px gap between every
        // row would both waste vertical space and swallow clicks aimed at the rows.
        if (!isLayerDragActive())
            return;

        ImGui::PushID(static_cast<int>(zone) + 10000); // offset so it cannot collide with a row id
        const float width = std::max(ImGui::GetContentRegionAvail().x, 10.0f);
        ImGui::InvisibleButton("##insertZone", ImVec2(width, 4.0f));

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kLayerDragPayload))
            {
                uint64_t draggedId = 0;
                std::memcpy(&draggedId, payload->Data, sizeof(uint64_t));

                // A zone index is NOT the destination index: dragging downwards lifts the row out
                // before re-inserting it, so everything below its old slot has already shifted up.
                // That arithmetic lives in Services precisely so it can be tested.
                if (const auto target = services::moveTargetForInsertZone(stack, draggedId,
                                                                          static_cast<uint32_t>(zone)))
                {
                    pendingMoveId = draggedId;
                    pendingMoveIndex = *target;
                }
            }
            ImGui::EndDragDropTarget();
        }

        // A drop line the artist can actually see. ImGui draws nothing for an InvisibleButton.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        {
            const ImVec2 lo = ImGui::GetItemRectMin();
            const ImVec2 hi = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(lo.x, (lo.y + hi.y) * 0.5f),
                                                ImVec2(hi.x, (lo.y + hi.y) * 0.5f),
                                                ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
        }

        ImGui::PopID();
    }

    void HeightLayerPanel::drawRow(const services::HeightLayerInfo& info, size_t index,
                                   bool interactive)
    {
        ImGui::PushID(static_cast<int>(info.id));

        const bool isRenaming = (renamingId == info.id);
        const bool isSelected = (selectedId == info.id);

        // A hidden layer reads as dimmed rather than as absent: it is still in the stack, still
        // covers its tiles, and still composes when shown again.
        if (!info.visible)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        const std::string label = displayName(info);
        // AllowOverlap so the right-aligned eye button gets its own clicks instead of the row
        // swallowing them.
        if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowOverlap))
            selectedId = info.id;

        if (!info.visible)
            ImGui::PopStyleColor();

        // Immediately after the item and before any SameLine, or the payload binds to the wrong
        // widget.
        if (interactive && !isRenaming && ImGui::BeginDragDropSource())
        {
            const uint64_t payload = info.id;
            ImGui::SetDragDropPayload(kLayerDragPayload, &payload, sizeof(uint64_t));
            ImGui::Text("Move %s", label.c_str());
            ImGui::EndDragDropSource();
        }

        if (interactive && !isRenaming && ImGui::IsItemHovered()
            && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            renamingId = info.id;
            renameFocusPending = true;
            const size_t count = std::min(label.size(), renameBuffer.size() - 1);
            std::memcpy(renameBuffer.data(), label.data(), count);
            renameBuffer[count] = '\0';
        }

        // Bound explicitly to the row. A bare BeginPopupContextItem() binds to the LAST drawn item,
        // which is the eye button below, so right-clicking the row itself would do nothing.
        if (interactive)
            ImGui::OpenPopupOnItemClick("##layerCtx", ImGuiPopupFlags_MouseButtonRight);

        // Tile count, so an artist can tell a road corridor from a stray one-tile layer.
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 96.0f);
        ImGui::TextDisabled("%u tile%s", info.affectedTileCount,
                            info.affectedTileCount == 1 ? "" : "s");

        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (ImGui::SmallButton(info.visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH))
        {
            pendingVisibilityId = info.id;
            pendingVisibilityValue = !info.visible;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(info.visible ? "Hide" : "Show");

        if (isRenaming)
        {
            if (renameFocusPending)
            {
                ImGui::SetKeyboardFocusHere();
                renameFocusPending = false;
            }
            ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 10.0f, 80.0f));
            const bool committed =
                ImGui::InputText("##rename", renameBuffer.data(), renameBuffer.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue
                                     | ImGuiInputTextFlags_AutoSelectAll);

            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                renamingId = 0;
            }
            else if (committed || ImGui::IsItemDeactivated())
            {
                // An empty name is legal in the format, but it would leave an unidentifiable row,
                // so the field treats "cleared" as "cancelled".
                if (renameBuffer[0] != '\0')
                {
                    pendingRenameId = info.id;
                    pendingRenameValue = renameBuffer.data();
                }
                renamingId = 0;
            }
        }

        if (ImGui::BeginPopup("##layerCtx"))
        {
            if (ImGui::MenuItem("Rename"))
            {
                renamingId = info.id;
                renameFocusPending = true;
                const size_t count = std::min(label.size(), renameBuffer.size() - 1);
                std::memcpy(renameBuffer.data(), label.data(), count);
                renameBuffer[count] = '\0';
            }
            if (ImGui::MenuItem(info.visible ? "Hide" : "Show"))
            {
                pendingVisibilityId = info.id;
                pendingVisibilityValue = !info.visible;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete"))
                pendingDeleteId = info.id;
            ImGui::EndPopup();
        }

        // Precision reorder for anyone who would rather not drag. Same command underneath, so it
        // records the same undo entry.
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 56.0f);
        ImGui::BeginDisabled(!interactive || index == 0);
        if (ImGui::SmallButton(ICON_FA_ARROW_UP))
        {
            pendingMoveId = info.id;
            pendingMoveIndex = static_cast<uint32_t>(index - 1);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!interactive || index + 1 >= stack.size());
        if (ImGui::SmallButton(ICON_FA_ARROW_DOWN))
        {
            pendingMoveId = info.id;
            pendingMoveIndex = static_cast<uint32_t>(index + 1);
        }
        ImGui::EndDisabled();

        ImGui::PopID();
    }

    void HeightLayerPanel::applyPendingEdits()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (pendingDeleteId != 0)
        {
            // Computed from the PRE-delete cache: the post-delete list does not exist yet, and the
            // notification that will rebuild it has not fired.
            selectedId = services::selectionAfterDelete(stack, pendingDeleteId, selectedId);

            // DeleteSplineCommand rather than the raw layer removal: it is the Editor-facing
            // delete, it records the undo entry, and it is the one place that also drops the
            // service's applied-spline bookkeeping.
            events::splineTerrain::DeleteSplineCommand cmd;
            cmd.splineId = pendingDeleteId;
            dispatcher.execute(cmd);

            pendingDeleteId = 0;
            stackDirty = true;
        }

        // query(), not execute(): these return a value, so they are registered as query handlers
        // and execute() would throw straight past this frame.
        if (pendingVisibilityId != 0)
        {
            events::splineTerrain::SetHeightLayerVisibleWithUndoCommand cmd;
            cmd.splineId = pendingVisibilityId;
            cmd.visible = pendingVisibilityValue;
            dispatcher.query(cmd);

            pendingVisibilityId = 0;
            stackDirty = true;
        }

        if (pendingMoveId != 0)
        {
            events::splineTerrain::MoveHeightLayerWithUndoCommand cmd;
            cmd.splineId = pendingMoveId;
            cmd.newIndex = pendingMoveIndex;
            dispatcher.query(cmd);

            pendingMoveId = 0;
            stackDirty = true;
        }

        if (pendingRenameId != 0)
        {
            events::splineTerrain::RenameHeightLayerWithUndoCommand cmd;
            cmd.splineId = pendingRenameId;
            cmd.name = pendingRenameValue;
            dispatcher.query(cmd);

            pendingRenameId = 0;
            pendingRenameValue.clear();
            stackDirty = true;
        }
    }

    void HeightLayerPanel::draw()
    {
        subscribe();
        if (!visible)
            return;

        ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Height Layers", &visible))
        {
            ImGui::End();
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();
        const bool locked =
            dispatcher.query(events::terrain::IsHeightLayerEditingLockedQuery{});
        const auto progress =
            dispatcher.query(events::terrain::GetHeightLayerRecomposeProgressQuery{});

        // An empty list on a terrain that demonstrably has layers on disk is the most alarming
        // thing this panel can show, so it says why instead of showing nothing.
        if (locked)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.25f, 1.0f));
            ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION
                               " Layer editing is disabled: this terrain's .vfterrainlayers sidecar "
                               "could not be read. The terrain is loaded from its flattened heights.");
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        // AC: report progress, and block further stack edits while a wide invalidation settles.
        //
        // Framed honestly. A layer op composes SYNCHRONOUSLY and unbudgeted, so by the time this
        // polls, the recompose itself is already done and pendingResident is zero — what drains
        // here is the 8-tiles-per-frame MESH queue behind it. Disabling the controls stops the
        // artist stacking a second multi-second invalidation onto an unsettled one; it is not a
        // race guard, because a stack op has no frame boundary inside it for a race to occur in.
        if (progress.active)
        {
            ImGui::ProgressBar(progress.progress, ImVec2(-1.0f, 0.0f));
            ImGui::Text("Rebuilding %u of %u tile(s)", progress.meshBacklog, progress.totalAtStart);
            if (progress.pendingUnloaded > 0)
            {
                // Reported apart from the bar on purpose: these wait on the streamer, not on the
                // recompose budget, so folding them in would pin the bar under 100% for as long as
                // the camera stayed away and read as a hang.
                ImGui::TextDisabled("%u tile(s) waiting on the streamer", progress.pendingUnloaded);
            }
            ImGui::Separator();
        }

        if (stackDirty)
            refresh();

        const bool interactive = !locked && !progress.active;

        if (stack.empty())
        {
            ImGui::TextDisabled(locked ? "Layer stack unavailable."
                                       : "No height layers. Apply a sculpt spline to create one.");
        }

        ImGui::BeginDisabled(!interactive);
        for (size_t i = 0; i < stack.size(); ++i)
        {
            drawInsertZone(i);
            drawRow(stack[i], i, interactive);
        }
        drawInsertZone(stack.size()); // the trailing "move to the bottom" target
        ImGui::EndDisabled();

        if (!stack.empty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("Composition order: top of the list composes first.");
        }

        // AC: paint is EXCLUDED, and visibly so — mere absence would read as an oversight.
        ImGui::Separator();
        ImGui::TextDisabled(ICON_FA_CIRCLE_INFO " Height layers only. Paint layers are not available.");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Painting is destructive: applying a weight brush can evict a "
                              "material channel and renormalize the whole tile, so a paint "
                              "contribution is not a pure function of its parameters and cannot be "
                              "replayed as a layer.\n\nLayered paint is gated on a conflict and "
                              "reduction design that has not been approved "
                              "(docs/TERRAIN_EDIT_LAYERS.md).");
        }

        ImGui::End();

        // After End(). Each of these publishes HeightLayerStackChangedNotification synchronously,
        // and running that inside the window would leave subscribers drawing into a window this
        // panel has already finished with.
        applyPendingEdits();
    }
}
