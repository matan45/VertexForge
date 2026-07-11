#include "ViewPortSelection.hpp"
#include "../../camera/EditorCamera.hpp"
#include "../../selection/SelectionPolicy.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/terrain/CaveModeEvents.hpp"
#include "events/terrain/SplineTerrainEvents.hpp"
#include "events/meshbrush/MeshBrushEvents.hpp"
#include "events/ui/UIPickEvents.hpp"
#include "math/ScreenRegionFrustum.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/EntityConversion.hpp"
#include <imgui.h>
#include "ImGuizmo.h"
#include <algorithm>

namespace
{
    bool sameSelection(const std::vector<services::EntityHandle>& a,
                       const std::vector<services::EntityHandle>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].id != b[i].id) return false;
        }
        return true;
    }
}

namespace windows
{
    void ViewPortSelection::handleEntityClick(services::EntityHandle picked)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        const ImGuiIO& io = ImGui::GetIO();
        selection::Modifiers mods{io.KeyCtrl, io.KeyShift};

        // Mesh-brush mode: Ctrl+click is the brush's own pick affordance and
        // stays a replace-select — toggle semantics would leave no way to
        // replace the selection while brushing.
        if (dispatcher.query(events::meshBrush::IsMeshBrushModeActiveQuery{}))
        {
            mods = {};
        }

        auto current = dispatcher.query(events::scene::GetSelectedEntitiesQuery{});
        events::scene::SelectEntitiesCommand cmd;
        cmd.entities = selection::computeClickSelection(current, picked, mods);
        dispatcher.execute(cmd);
    }

    void ViewPortSelection::beginMarqueeCandidate(glm::vec2 mousePos)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // The click guards in ViewPort::handleEntityPicking already exclude play
        // mode, sculpt/paint/hole/vegetation, camera look and the gizmo. Cave and
        // spline are absent from those (pre-existing) click guards, and mesh-brush
        // reaches here with Ctrl held — none of them may start a marquee or an
        // empty-click clear.
        if (dispatcher.query(events::cave::IsCaveModeActiveQuery{})) return;
        if (dispatcher.query(events::splineTerrain::IsSplineModeActiveQuery{})) return;
        if (dispatcher.query(events::meshBrush::IsMeshBrushModeActiveQuery{})) return;

        // Clicking overlay widgets (gizmo toolbar etc.) must not clear the
        // selection even when empty viewport space sits behind them. A pressed
        // button is the active item; the viewport ImGui::Image never activates,
        // so this does not block marquees started over the image itself.
        if (ImGui::IsAnyItemActive()) return;

        marqueePending = true;
        marqueeActive = false;
        marqueeAnchor = mousePos;
    }

    void ViewPortSelection::resetMarquee()
    {
        marqueePending = false;
        marqueeActive = false;
    }

    void ViewPortSelection::update(const ViewPortPicker& picker,
                                   const editor::EditorCamera& camera,
                                   bool isPlayMode, glm::vec2 viewportPos,
                                   glm::vec2 viewportSize)
    {
        if (isPlayMode)
        {
            resetMarquee();
            return;
        }
        if (!marqueePending) return;

        // RMB (camera look), Esc, or a gizmo grab abandons the marquee with no
        // selection change.
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
            ImGuizmo::IsUsing())
        {
            resetMarquee();
            return;
        }

        ImVec2 mouse = ImGui::GetMousePos();
        glm::vec2 mousePos(mouse.x, mouse.y);

        if (!marqueeActive)
        {
            constexpr float dragThreshold = 4.0f;
            if (glm::length(mousePos - marqueeAnchor) > dragThreshold)
            {
                marqueeActive = true;
            }
        }

        if (marqueeActive)
        {
            // Marquee in the editor selection accent #FFA100.
            const ImU32 border = IM_COL32(255, 161, 0, 255);
            const ImU32 fill = IM_COL32(255, 161, 0, 32);
            ImVec2 rectMin(std::min(marqueeAnchor.x, mousePos.x),
                           std::min(marqueeAnchor.y, mousePos.y));
            ImVec2 rectMax(std::max(marqueeAnchor.x, mousePos.x),
                           std::max(marqueeAnchor.y, mousePos.y));
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(rectMin, rectMax, fill);
            drawList->AddRect(rectMin, rectMax, border);
        }

        if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) return;

        const bool wasActive = marqueeActive;
        resetMarquee();

        auto& dispatcher = events::EventDispatcher::instance();
        const ImGuiIO& io = ImGui::GetIO();
        const selection::Modifiers mods{io.KeyCtrl, io.KeyShift};
        auto current = dispatcher.query(events::scene::GetSelectedEntitiesQuery{});

        events::scene::SelectEntitiesCommand cmd;
        if (wasActive)
        {
            auto hits = collectRegionHits(picker, camera, marqueeAnchor, mousePos,
                                          viewportPos, viewportSize);
            cmd.entities = selection::computeMarqueeSelection(current, hits, mods);
        }
        else
        {
            // Plain empty click clears; Ctrl/Shift-clicking empty space preserves.
            cmd.entities = selection::computeEmptyClick(current, mods);
        }

        if (!sameSelection(cmd.entities, current))
        {
            dispatcher.execute(cmd);
        }
    }

    std::vector<services::EntityHandle> ViewPortSelection::collectRegionHits(
        const ViewPortPicker& picker, const editor::EditorCamera& camera,
        glm::vec2 cornerA, glm::vec2 cornerB,
        glm::vec2 viewportPos, glm::vec2 viewportSize) const
    {
        std::vector<services::EntityHandle> hits;
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return hits;

        // Clamp to the viewport content rect (same clamping as screenToWorldRay).
        const glm::vec2 lo = glm::clamp(glm::min(cornerA, cornerB), viewportPos,
                                        viewportPos + viewportSize);
        const glm::vec2 hi = glm::clamp(glm::max(cornerA, cornerB), viewportPos,
                                        viewportPos + viewportSize);

        auto& registry = scene::EntityRegistry::getRegistry();

        // Hit order mirrors the click pick priority: billboards, UI, meshes.
        for (const auto& hit : picker.getBillboardHits())
        {
            const glm::vec2 halfSize = hit.screenSize * 0.5f;
            const glm::vec2 minB = hit.screenCenter - halfSize;
            const glm::vec2 maxB = hit.screenCenter + halfSize;
            if (minB.x <= hi.x && maxB.x >= lo.x && minB.y <= hi.y && maxB.y >= lo.y)
            {
                auto entity = services::internal::fromHandle(hit.entity);
                if (registry.valid(entity) &&
                    registry.all_of<components::BillboardComponent>(entity))
                {
                    hits.push_back(hit.entity);
                }
            }
        }

        events::ui::PickUIEntitiesInRegionQuery uiQuery;
        uiQuery.minPx = lo;
        uiQuery.maxPx = hi;
        uiQuery.viewportPos = viewportPos;
        uiQuery.viewportSize = viewportSize;
        uiQuery.viewMatrix = camera.getViewMatrix();
        uiQuery.projMatrix = camera.getProjectionMatrix();
        auto uiHits = events::EventDispatcher::instance().query(uiQuery);
        hits.insert(hits.end(), uiHits.begin(), uiHits.end());

        // Meshes: world AABB vs the screen-rect region frustum (viewport-relative
        // pixels, same NDC mapping as screenToWorldRay — no extra Y flip).
        const math::Frustum region = math::buildScreenRegionFrustum(
            camera.getViewMatrix(), camera.getProjectionMatrix(),
            lo - viewportPos, hi - viewportPos, viewportSize.x, viewportSize.y);
        for (const auto& meshData : picker.getMeshHits())
        {
            if (!region.intersectsAABB(meshData.worldAABB)) continue;

            auto entity = services::internal::fromHandle(meshData.entity);
            if (registry.valid(entity) &&
                registry.all_of<components::MeshComponent>(entity))
            {
                hits.push_back(meshData.entity);
            }
        }
        return hits;
    }
}
