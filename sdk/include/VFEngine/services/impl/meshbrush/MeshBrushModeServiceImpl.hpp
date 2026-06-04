#pragma once
#include "../../interfaces/meshbrush/IMeshBrushModeService.hpp"
#include "../../events/EventTypes.hpp"

namespace services
{
    class MeshBrushModeServiceImpl : public IMeshBrushModeService
    {
    private:
        bool meshBrushActive = false;

        ::events::SubscriptionToken editorModeToken;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken sculptModeToken;
        ::events::SubscriptionToken paintModeToken;
        ::events::SubscriptionToken holeModeToken;
        ::events::SubscriptionToken vegetationModeToken;

    public:
        MeshBrushModeServiceImpl() = default;
        ~MeshBrushModeServiceImpl() override;

        void registerEventHandlers() override;

        void activate() override;
        void deactivate() override;
        bool isActive() const override;
    };
}
