#pragma once
#include "UICommon.hpp"
#include "../../render/ui/UIRenderTypes.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <vector>

namespace controllers::offscreen
{
    struct FrameContext;

    namespace ui_screenspace
    {
        using ScrollContainerMap = std::unordered_map<uint32_t, ui_common::ScrollContainerInfo>;

        // Scroll interaction
        bool processActiveScrollDrags(entt::registry& registry, const FrameContext& ctx);
        void initiateScrollThumbDrag(entt::registry& registry, const FrameContext& ctx);
        void processScrollWheelInput(entt::registry& registry, const FrameContext& ctx, bool anyScrollDragging);
        ScrollContainerMap buildScrollContainerData(entt::registry& registry, const FrameContext& ctx);

        // Widget draw data
        void generateSliderDrawData(
            entt::registry& registry, const FrameContext& ctx,
            const ScrollContainerMap& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList);

        void generateProgressBarDrawData(
            entt::registry& registry, const FrameContext& ctx,
            const ScrollContainerMap& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList);

        void generateScrollbarDrawData(
            entt::registry& registry, const FrameContext& ctx,
            std::vector<render::ui::UIImageRenderData>& drawList);

        void generateTextInputCaretDrawData(
            entt::registry& registry, const FrameContext& ctx,
            entt::entity focusedEntity,
            std::vector<render::ui::UIImageRenderData>& drawList);

        void generateDropdownDrawData(
            entt::registry& registry, const FrameContext& ctx,
            std::vector<render::ui::UIImageRenderData>& drawList);

        void generateDragGhostDrawData(
            entt::registry& registry, const FrameContext& ctx,
            std::vector<render::ui::UIImageRenderData>& drawList);
    }
}
