#include "RuntimePickerServiceImpl.hpp"
#include "../../providers/input/IRuntimePickerProvider.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/input/RuntimePickerEvents.hpp"
#include <cassert>

namespace services
{
    RuntimePickerServiceImpl::RuntimePickerServiceImpl(IRuntimePickerProvider* provider)
        : pickerProvider(provider)
    {
        assert(pickerProvider && "RuntimePickerProvider must not be null");
    }

    void RuntimePickerServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerQueryHandler<::events::input::ScreenToWorldRayQuery>(
            [this](const ::events::input::ScreenToWorldRayQuery& q) -> std::optional<PickRay>
            {
                PickRay ray;
                if (!pickerProvider->screenToWorldRay(q.screenPos, ray))
                    return std::nullopt;
                return ray;
            });

        dispatcher.registerQueryHandler<::events::input::PickTerrainQuery>(
            [this](const ::events::input::PickTerrainQuery& q) -> std::optional<glm::vec3>
            {
                PickRay ray;
                if (!pickerProvider->screenToWorldRay(q.screenPos, ray))
                    return std::nullopt;
                return pickerProvider->pickTerrain(ray);
            });

        dispatcher.registerQueryHandler<::events::input::PickEntityQuery>(
            [this](const ::events::input::PickEntityQuery& q) -> RaycastHit
            {
                PickRay ray;
                if (!pickerProvider->screenToWorldRay(q.screenPos, ray))
                    return RaycastHit{};
                return pickerProvider->pickEntity(ray, q.layerMask);
            });
    }
}
