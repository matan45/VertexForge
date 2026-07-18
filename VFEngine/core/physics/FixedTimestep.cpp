#include "FixedTimestep.hpp"
#include "FixedTimestepMath.hpp"

namespace core::physics
{
    FixedTimestepResult FixedTimestep::update(double deltaTime, const std::function<void(float)>& physicsStep)
    {
        // Accumulator + spiral-of-death guards + drop-remainder live in the
        // Jolt-free, unit-tested FixedTimestepMath.hpp.
        FixedStepPlan plan = planFixedSteps(accumulator, deltaTime, timestep, maxAccumulator, maxStepsPerFrame);

        for (int i = 0; i < plan.steps; ++i)
        {
            if (physicsStep)
            {
                physicsStep(static_cast<float>(timestep));
            }
        }

        FixedTimestepResult result;
        result.stepsTaken = plan.steps;
        result.alpha = plan.alpha;
        return result;
    }
}
