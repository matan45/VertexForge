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

        // Widget draw data.
        // VK-1435: each takes a defaulted `scopedCanvas = entt::null`. With entt::null (the main
        // screen-space pass) behavior is byte-identical. When set (the UI Layer Builder offscreen
        // preview), only entities whose owning canvas == scopedCanvas are emitted, and the active
        // check is scoped (treat the tagged sandbox root as active) — same safe pattern as the
        // label emitters in UIFrameBuilder.cpp.
        void generateSliderDrawData(
            entt::registry& registry, const FrameContext& ctx,
            const ScrollContainerMap& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList,
            entt::entity scopedCanvas = entt::null);

        void generateProgressBarDrawData(
            entt::registry& registry, const FrameContext& ctx,
            const ScrollContainerMap& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList,
            entt::entity scopedCanvas = entt::null);

        void generateScrollbarDrawData(
            entt::registry& registry, const FrameContext& ctx,
            std::vector<render::ui::UIImageRenderData>& drawList,
            entt::entity scopedCanvas = entt::null);

        void generateTextInputCaretDrawData(
            entt::registry& registry, const FrameContext& ctx,
            entt::entity focusedEntity,
            std::vector<render::ui::UIImageRenderData>& drawList,
            entt::entity scopedCanvas = entt::null);

        void generateDropdownDrawData(
            entt::registry& registry, const FrameContext& ctx,
            std::vector<render::ui::UIImageRenderData>& drawList,
            entt::entity scopedCanvas = entt::null);

        void generateDragGhostDrawData(
            entt::registry& registry, const FrameContext& ctx,
            std::vector<render::ui::UIImageRenderData>& drawList,
            entt::entity scopedCanvas = entt::null);

        void generateListSelectionDrawData(
            entt::registry& registry, const FrameContext& ctx,
            const ScrollContainerMap& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList,
            entt::entity scopedCanvas = entt::null);
    }
}
