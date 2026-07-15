#pragma once

#include "data/EntityHandle.hpp"
#include "data/UndoTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include <string>

namespace windows
{
    class AudioSource3DDistanceUndoCommand : public services::IUndoableCommand
    {
    public:
        AudioSource3DDistanceUndoCommand(services::EntityHandle entity,
                                         float beforeMinDistance,
                                         float beforeMaxDistance,
                                         float afterMinDistance,
                                         float afterMaxDistance)
            : entity(entity)
            , beforeMinDistance(beforeMinDistance)
            , beforeMaxDistance(beforeMaxDistance)
            , afterMinDistance(afterMinDistance)
            , afterMaxDistance(afterMaxDistance)
        {
        }

        void execute() override { apply(afterMinDistance, afterMaxDistance); }
        void undo() override { apply(beforeMinDistance, beforeMaxDistance); }
        std::string getDescription() const override { return "Adjust Audio Attenuation"; }

    private:
        void apply(float minDistance, float maxDistance)
        {
            events::scene::SetAudioSource3DDistancesCommand command;
            command.entity = entity;
            command.minDistance = minDistance;
            command.maxDistance = maxDistance;
            ::events::EventDispatcher::instance().execute(command);
        }

        services::EntityHandle entity;
        float beforeMinDistance;
        float beforeMaxDistance;
        float afterMinDistance;
        float afterMaxDistance;
    };
}
