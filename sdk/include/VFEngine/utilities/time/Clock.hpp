#pragma once
#include <chrono>

namespace engineTime {

	using Clock = std::chrono::high_resolution_clock;  // High-resolution clock for precise timing
	using TimePoint = std::chrono::time_point<Clock>;  // Time point for tracking specific times
	using Duration = std::chrono::duration<double>;    // Duration type to store time differences
}
