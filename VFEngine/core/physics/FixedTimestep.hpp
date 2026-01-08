#pragma once

#include <functional>

namespace core::physics {

    class FixedTimestep {
    public:
        static constexpr double PHYSICS_TIMESTEP = 1.0 / 60.0;  // 60 Hz physics
        static constexpr double MAX_ACCUMULATOR = 0.25;         // Max 250ms to prevent spiral of death
        static constexpr int MAX_STEPS_PER_FRAME = 8;           // Limit steps per frame

        FixedTimestep() = default;
        ~FixedTimestep() = default;

        // Call each frame with variable delta time
        // Executes physicsStep callback for each fixed timestep iteration
        // Returns number of physics steps taken
        int update(double deltaTime, const std::function<void(float)>& physicsStep);

        // Get interpolation factor for rendering (0.0 to 1.0)
        // Useful for smooth rendering between physics states
        double getAlpha() const { return accumulator / PHYSICS_TIMESTEP; }

        // Reset accumulator (e.g., when starting/stopping simulation)
        void reset() { accumulator = 0.0; }

        // Getters
        double getAccumulator() const { return accumulator; }
        static double getTimestep() { return PHYSICS_TIMESTEP; }

    private:
        double accumulator = 0.0;
    };

}
