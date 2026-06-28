#pragma once

// VK-1443 — UI Layer Builder hierarchy pane sub-controller (split out of UILayerBuilderWindow).
//
// A STATELESS pane: it holds only a back-reference to the owning window and reaches ALL window
// state + helper methods through `w.` (friend access). Behavior is byte-identical to the methods
// that previously lived on UILayerBuilderWindow — this is a pure structural relocation.

#include "data/EntityHandle.hpp"

namespace windows { class UILayerBuilderWindow; }

namespace windows::uilayer
{
    class UILayerHierarchyPane
    {
    public:
        explicit UILayerHierarchyPane(UILayerBuilderWindow& w) : w(w) {}

        void draw();

    private:
        UILayerBuilderWindow& w;

        void drawNode(services::EntityHandle entity, int depth);

        // Drag-drop reparent payload id for the hierarchy.
        static constexpr const char* kDragPayload = "DND_UILAYER_ENTITY";
    };
}
