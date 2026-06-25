#pragma once

// VK-1443 — UI Layer Builder canvas pane sub-controller (split out of UILayerBuilderWindow).
//
// A STATELESS pane: it holds only a back-reference to the owning window and reaches ALL window
// state + helper methods through `w.` (friend access). Behavior is byte-identical to the methods
// that previously lived on UILayerBuilderWindow — this is a pure structural relocation.

#include "UILayerCanvasHandles.hpp"
#include <glm/glm.hpp>

struct ImDrawList;

namespace windows { class UILayerBuilderWindow; }

namespace windows::uilayer
{
    class UILayerCanvasPane
    {
    public:
        explicit UILayerCanvasPane(UILayerBuilderWindow& w) : w(w) {}

        void draw();

    private:
        UILayerBuilderWindow& w;

        void drawCanvasImageAndHandles(glm::vec2 regionOrigin, glm::vec2 regionSize);
        void drawHandleOverlay(ImDrawList* dl, const uilayer::LetterboxMapping& map,
                               const uilayer::RefRect& rect);
        void handleCanvasInput(const uilayer::LetterboxMapping& map);
    };
}
