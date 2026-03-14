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
