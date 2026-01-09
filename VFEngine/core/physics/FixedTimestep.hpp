#pragma once

#include <functional>

namespace core::physics {

    class FixedTimestep {
    public:
        // Default values
        static constexpr double DEFAULT_TIMESTEP = 1.0 / 60.0;      // 60 Hz physics
        static constexpr double DEFAULT_MAX_ACCUMULATOR = 0.25;     // Max 250ms to prevent spiral of death
        static constexpr int DEFAULT_MAX_STEPS_PER_FRAME = 8;       // Limit steps per frame

        FixedTimestep() = default;
        ~FixedTimestep() = default;

        // Call each frame with variable delta time
        // Executes physicsStep callback for each fixed timestep iteration
        // Returns number of physics steps taken
        int update(double deltaTime, const std::function<void(float)>& physicsStep);

        // Get interpolation factor for rendering (0.0 to 1.0)
        // Useful for smooth rendering between physics states
        double getAlpha() const { return accumulator / timestep; }

        // Reset accumulator (e.g., when starting/stopping simulation)
        void reset() { accumulator = 0.0; }

        // Getters
        double getAccumulator() const { return accumulator; }
        double getTimestep() const { return timestep; }
        double getMaxAccumulator() const { return maxAccumulator; }
        int getMaxStepsPerFrame() const { return maxStepsPerFrame; }

        // Setters for configurable values
        void setTimestep(double value) { timestep = value > 0.0 ? value : DEFAULT_TIMESTEP; }
        void setMaxAccumulator(double value) { maxAccumulator = value > 0.0 ? value : DEFAULT_MAX_ACCUMULATOR; }
        void setMaxStepsPerFrame(int value) { maxStepsPerFrame = value > 0 ? value : DEFAULT_MAX_STEPS_PER_FRAME; }

    private:
        double accumulator = 0.0;
        double timestep = DEFAULT_TIMESTEP;
        double maxAccumulator = DEFAULT_MAX_ACCUMULATOR;
        int maxStepsPerFrame = DEFAULT_MAX_STEPS_PER_FRAME;
    };

}
