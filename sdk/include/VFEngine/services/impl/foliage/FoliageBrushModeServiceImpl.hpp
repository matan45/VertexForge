#pragma once
// VK-1575 (Phase 4): foliage brush mode toggle. Structural analog of
// meshbrush/MeshBrushModeServiceImpl — activating it deactivates every sibling terrain/
// brush mode, and it self-deactivates when any sibling activates or the editor enters Play.
// Foliage paints against the global terrain height query, so (unlike sculpt/paint/hole/cave)
// it holds no target-terrain entity.
#include "../../interfaces/foliage/IFoliageBrushModeService.hpp"
#include "../../events/EventTypes.hpp"

namespace services
{
    class FoliageBrushModeServiceImpl : public IFoliageBrushModeService
    {
    private:
        bool foliageBrushActive = false;

        ::events::SubscriptionToken editorModeToken;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken sculptModeToken;
        ::events::SubscriptionToken paintModeToken;
        ::events::SubscriptionToken holeModeToken;
        ::events::SubscriptionToken caveModeToken;
        ::events::SubscriptionToken vegetationBrushModeToken;
        ::events::SubscriptionToken meshBrushModeToken;

    public:
        FoliageBrushModeServiceImpl() = default;
        ~FoliageBrushModeServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void activate();
        void deactivate();
        bool isActive() const;
    };
}
