#pragma once
#include "ViewPortPicker.hpp"
#include "data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace editor
{
    class EditorCamera;
}

namespace windows
{
    // VK-1490: Unity-style viewport selection. Owns the modifier-aware click
    // dispatch and the drag-marquee state machine; the ordered-selection policy
    // itself lives in editor/selection/SelectionPolicy.hpp so it stays
    // unit-testable. ViewPort::handleEntityPicking keeps its guard list and
    // delegates only the post-pick decision here.
    class ViewPortSelection
    {
    private:
        bool marqueePending = false;    // LMB pressed on empty space, waiting for drag/release
        bool marqueeActive = false;     // drag passed the threshold, rectangle visible
        glm::vec2 marqueeAnchor{0.0f};  // press position, absolute screen coords

    public:
        // Click landed on a picked entity (existing billboard -> UI -> mesh
        // priority). Applies the modifier policy and dispatches the selection.
        void handleEntityClick(services::EntityHandle picked);

        // Click landed on empty viewport space: candidate for a marquee drag or,
        // if released without dragging, an empty-click clear. Only latches when
        // no tool mode owns the LMB (cave/spline/mesh-brush) and no overlay
        // widget is hovered.
        void beginMarqueeCandidate(glm::vec2 mousePos);

        // Per-frame update: drag threshold, marquee overlay rect, cancel keys,
        // and release resolution. Call every frame from ViewPort::draw while
        // the viewport window is current (the overlay draws into its DrawList).
        void update(const ViewPortPicker& picker, const editor::EditorCamera& camera,
                    bool isPlayMode, glm::vec2 viewportPos, glm::vec2 viewportSize,
                    bool customGizmoConsumesMouse = false);

        bool isMarqueeActive() const { return marqueeActive; }

    private:
        void resetMarquee();
        std::vector<services::EntityHandle> collectRegionHits(
            const ViewPortPicker& picker, const editor::EditorCamera& camera,
            glm::vec2 cornerA, glm::vec2 cornerB,
            glm::vec2 viewportPos, glm::vec2 viewportSize) const;
    };
}
