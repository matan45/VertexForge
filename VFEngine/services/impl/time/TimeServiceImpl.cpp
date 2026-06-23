#include "TimeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/time/TimeEvents.hpp"
#include "time/Timer.hpp"
#include <algorithm>

namespace services
{
    float TimeServiceImpl::clampTimeScale(float scale)
    {
        return std::clamp(scale, 0.0f, 8.0f);
    }

    void TimeServiceImpl::setGlobalTimeScale(float scale)
    {
        const float clamped = clampTimeScale(scale);
        engineTime::Timer::setTimeScale(static_cast<double>(clamped));

        ::events::time::GlobalTimeScaleChangedNotification notification;
        notification.scale = clamped;
        ::events::EventDispatcher::instance().publish(notification);
    }

    engineTime::FrameTime TimeServiceImpl::getTimeSnapshot()
    {
        return engineTime::FrameTime::current();
    }

    void TimeServiceImpl::freeze()
    {
        engineTime::Timer::freeze();
    }

    void TimeServiceImpl::unfreeze()
    {
        engineTime::Timer::unfreeze();
    }

    void TimeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<::events::time::SetGlobalTimeScaleCommand>(
            [this](const ::events::time::SetGlobalTimeScaleCommand& c)
            {
                setGlobalTimeScale(c.scale);
            });

        dispatcher.registerCommandHandler<::events::time::FreezeTimeCommand>(
            [this](const ::events::time::FreezeTimeCommand&)
            {
                freeze();
            });

        dispatcher.registerCommandHandler<::events::time::UnfreezeTimeCommand>(
            [this](const ::events::time::UnfreezeTimeCommand&)
            {
                unfreeze();
            });

        dispatcher.registerQueryHandler<::events::time::GetTimeSnapshotQuery>(
            [this](const ::events::time::GetTimeSnapshotQuery&) -> engineTime::FrameTime
            {
                return getTimeSnapshot();
            });
    }
}
