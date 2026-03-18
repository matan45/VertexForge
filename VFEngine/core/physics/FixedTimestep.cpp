#include "FixedTimestep.hpp"
#include <algorithm>

namespace core::physics
{
    FixedTimestepResult FixedTimestep::update(double deltaTime, const std::function<void(float)>& physicsStep)
    {
        deltaTime = std::min(deltaTime, maxAccumulator);

        accumulator += deltaTime;
        int stepsTaken = 0;

        while (accumulator >= timestep && stepsTaken < maxStepsPerFrame)
        {
            if (physicsStep)
            {
                physicsStep(static_cast<float>(timestep));
            }
            accumulator -= timestep;
            stepsTaken++;
        }

        FixedTimestepResult result;
        result.stepsTaken = stepsTaken;
        result.alpha = (timestep > 0.0) ? (accumulator / timestep) : 0.0;
        return result;
    }
}
