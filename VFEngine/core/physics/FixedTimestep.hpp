#pragma once

#include <functional>

namespace core::physics {

    class FixedTimestep {
    private:
        static constexpr double DEFAULT_TIMESTEP = 1.0 / 60.0;      // 60 Hz physics
        static constexpr double DEFAULT_MAX_ACCUMULATOR = 0.25;     // Max 250ms to prevent spiral of death
        static constexpr int DEFAULT_MAX_STEPS_PER_FRAME = 8;       // Limit steps per frame

        // Valid ranges to prevent crashes/freezes
        static constexpr double MIN_TIMESTEP = 1.0 / 240.0;         // Max 240 Hz (prevent CPU overload)
        static constexpr double MAX_TIMESTEP = 1.0 / 10.0;          // Min 10 Hz (prevent tunneling)
        static constexpr double MIN_MAX_ACCUMULATOR = 0.05;         // 50ms minimum
        static constexpr double MAX_MAX_ACCUMULATOR = 1.0;          // 1s maximum
        static constexpr int MIN_STEPS_PER_FRAME = 1;
        static constexpr int MAX_STEPS_PER_FRAME = 32;
        
        double accumulator = 0.0;
        double timestep = DEFAULT_TIMESTEP;
        double maxAccumulator = DEFAULT_MAX_ACCUMULATOR;
        int maxStepsPerFrame = DEFAULT_MAX_STEPS_PER_FRAME;
    public:

        FixedTimestep() = default;
        ~FixedTimestep() = default;
        
        int update(double deltaTime, const std::function<void(float)>& physicsStep);
        void reset() { accumulator = 0.0; }

        void setTimestep(double value) {
            timestep = clamp(value, MIN_TIMESTEP, MAX_TIMESTEP);
        }
        void setMaxAccumulator(double value) {
            maxAccumulator = clamp(value, MIN_MAX_ACCUMULATOR, MAX_MAX_ACCUMULATOR);
        }
        void setMaxStepsPerFrame(int value) {
            maxStepsPerFrame = clamp(value, MIN_STEPS_PER_FRAME, MAX_STEPS_PER_FRAME);
        }

    private:
        template<typename T>
        static constexpr T clamp(T value, T minVal, T maxVal) {
            return value < minVal ? minVal : (value > maxVal ? maxVal : value);
        }
    };

}
