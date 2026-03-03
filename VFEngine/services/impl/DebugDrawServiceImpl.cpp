#include "DebugDrawServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/DebugDrawEvents.hpp"
#include "../providers/IDebugDrawProvider.hpp"

namespace services
{
    DebugDrawServiceImpl::DebugDrawServiceImpl(IDebugDrawProvider* provider)
        : debugDrawProvider(provider)
    {
    }

    void DebugDrawServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::debugdraw::DrawLineCommand>(
            [this](const events::debugdraw::DrawLineCommand& cmd)
            {
                if (debugDrawProvider)
                {
                    debugDrawProvider->drawLine(cmd.start, cmd.end, cmd.color);
                }
            });

        dispatcher.registerCommandHandler<events::debugdraw::DrawRayCommand>(
            [this](const events::debugdraw::DrawRayCommand& cmd)
            {
                if (debugDrawProvider)
                {
                    debugDrawProvider->drawRay(cmd.origin, cmd.direction, cmd.length, cmd.color);
                }
            });

        dispatcher.registerCommandHandler<events::debugdraw::DrawBoxCommand>(
            [this](const events::debugdraw::DrawBoxCommand& cmd)
            {
                if (debugDrawProvider)
                {
                    debugDrawProvider->drawBox(cmd.center, cmd.halfExtents, cmd.color);
                }
            });

        dispatcher.registerCommandHandler<events::debugdraw::DrawSphereCommand>(
            [this](const events::debugdraw::DrawSphereCommand& cmd)
            {
                if (debugDrawProvider)
                {
                    debugDrawProvider->drawSphere(cmd.center, cmd.radius, cmd.color);
                }
            });

        dispatcher.registerCommandHandler<events::debugdraw::SetDebugDrawEnabledCommand>(
            [this](const events::debugdraw::SetDebugDrawEnabledCommand& cmd)
            {
                if (debugDrawProvider)
                {
                    debugDrawProvider->setEnabled(cmd.enabled);
                }
            });

        dispatcher.registerQueryHandler<events::debugdraw::GetDebugDrawEnabledQuery>(
            [this](const events::debugdraw::GetDebugDrawEnabledQuery&)
            {
                return debugDrawProvider ? debugDrawProvider->isEnabled() : false;
            });
    }
}
