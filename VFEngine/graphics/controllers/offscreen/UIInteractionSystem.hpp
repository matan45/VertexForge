#pragma once
#include <entt/entt.hpp>

namespace controllers::offscreen
{
    struct FrameContext;

    class UIInteractionSystem
    {
    public:
        UIInteractionSystem() = default;

        void processButtonInteraction(const FrameContext& ctx);
        void processCheckboxInteraction(const FrameContext& ctx);
        void processTextInputInteraction(const FrameContext& ctx);
        void processDropdownInteraction(const FrameContext& ctx);
        void processTabsInteraction(const FrameContext& ctx);
        void processSliderInteraction(const FrameContext& ctx);
        void processDragDropInteraction(const FrameContext& ctx);
        // Hover-delay tooltips: updates the UITooltipState registry-context
        // singleton (Text mode bubble placement / ChildPanel toggling).
        void processTooltipInteraction(const FrameContext& ctx);
        // Windows: maintains the UIModalState stack, title-bar dragging and
        // the close button. Must run BEFORE the other widget interactions so
        // modal gating sees this frame's stack.
        void processWindowInteraction(const FrameContext& ctx);
        // List views: click-to-select on item instance roots.
        void processListViewInteraction(const FrameContext& ctx);

        // Updates the registry-context UIPointerState: true when the cursor is
        // over any visible UI element (so scripts can skip world raycasts).
        void computePointerOverUI(const FrameContext& ctx);

        // VK-1442 — display-only edit-preview passes for the UI Layer Builder's scoped offscreen
        // render. They reuse the resting visual logic of the matching process* method WITHOUT any
        // hit-testing or notifications, scoped to `scopedCanvas` (the sandbox root). They hold no
        // cross-frame state, so the builder can call them on a throwaway instance.
        void applyCheckboxVisualScoped(const FrameContext& ctx, entt::entity scopedCanvas);
        void applyTabsActivePaneScoped(const FrameContext& ctx, entt::entity scopedCanvas);

        entt::entity getFocusedTextInput() const { return focusedTextInput; }

    private:
        entt::entity focusedTextInput = entt::null;

        // Drag-drop tag matching
        static bool tagMatches(const std::string& acceptTag, const std::string& dragTag);

        // Text input sub-methods
        void textInputHitTest(const FrameContext& ctx, entt::entity& hoveredTextInput);
        void textInputFocusManagement(const FrameContext& ctx, entt::entity hoveredTextInput);
        void textInputEditing(const FrameContext& ctx);
        void textInputStateAndVisuals(const FrameContext& ctx, entt::entity hoveredTextInput);
    };
}
