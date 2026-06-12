#include "UIInteractionSystem.hpp"
#include "FramePreparationSystem.hpp"  // for FrameContext
#include "UICommon.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIListViewEvents.hpp"
#include <limits>

using namespace controllers::offscreen::ui_common;

namespace controllers::offscreen
{
    void UIInteractionSystem::processListViewInteraction(const FrameContext& ctx)
    {
        if (!ctx.playModeActive || !ctx.leftMousePressed)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto listView = registry.view<components::UIListViewComponent>();
        if (listView.size() == 0)
            return;

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);
        auto scrollContainers = buildScrollContainerMap(registry, vw, vh);

        // Smallest-area item root under the cursor across all selectable lists
        entt::entity hitList = entt::null;
        int hitIndex = -1;
        float smallestArea = std::numeric_limits<float>::max();

        for (auto listEntity : listView)
        {
            auto& comp = listView.get<components::UIListViewComponent>(listEntity);
            if (!comp.selectable || comp.itemInstances.empty())
                continue;
            if (!scene::Entity::isEffectivelyActive(registry, listEntity))
                continue;
            if (!isInteractionAllowed(registry, listEntity))
                continue;

            const auto* canvas = findCanvasForEntity(registry, listEntity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(listEntity))
                canvas = &registry.get<components::UICanvasComponent>(listEntity);
            if (!canvas)
                continue;

            float scale = computeCanvasScale(canvas, vw, vh);

            for (size_t i = 0; i < comp.itemInstances.size(); ++i)
            {
                entt::entity item = comp.itemInstances[i];
                if (!registry.valid(item) ||
                    !registry.all_of<components::UIRectComponent>(item))
                    continue;
                if (!scene::Entity::isEffectivelyActive(registry, item))
                    continue;

                const auto& rectComp = registry.get<components::UIRectComponent>(item);
                PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

                auto [scrollAncestor, scissor] = findScrollInfo(registry, item, scrollContainers);
                applyScrollOffset(rect, scrollAncestor, scrollContainers);

                if (hitTestRect(ctx.mousePosition, rect, scissor))
                {
                    float area = rect.w * rect.h;
                    if (area < smallestArea)
                    {
                        smallestArea = area;
                        hitList = listEntity;
                        hitIndex = static_cast<int>(i);
                    }
                }
            }
        }

        if (hitList == entt::null)
            return;

        auto& comp = registry.get<components::UIListViewComponent>(hitList);
        if (comp.selectedIndex == hitIndex)
            return;

        int previous = comp.selectedIndex;
        comp.selectedIndex = hitIndex;

        auto [handle, name] = makeEntityPayload(registry, hitList);
        events::ui::UIListSelectionChangedNotification notif;
        notif.entity = handle;
        notif.entityName = std::move(name);
        notif.previousIndex = previous;
        notif.newIndex = hitIndex;
        events::EventDispatcher::instance().publish(notif);
    }
}
