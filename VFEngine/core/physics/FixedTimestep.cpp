#include "FixedTimestep.hpp"
#include <algorithm>

namespace core::physics {

    int FixedTimestep::update(double deltaTime, const std::function<void(float)>& physicsStep) {
        // Clamp delta time to prevent spiral of death
        // (when physics takes longer than real time, causing more physics steps)
        deltaTime = std::min(deltaTime, maxAccumulator);

        accumulator += deltaTime;
        int stepsTaken = 0;

        // Run fixed timestep updates until accumulator is depleted
        while (accumulator >= timestep && stepsTaken < maxStepsPerFrame) {
            if (physicsStep) {
                physicsStep(static_cast<float>(timestep));
            }
            accumulator -= timestep;
            stepsTaken++;
        }

        return stepsTaken;
    }

}
