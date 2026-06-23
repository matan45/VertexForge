#pragma once
#include "Timer.hpp"

namespace engineTime {

	// Immutable per-frame snapshot of the time authority (VK-992). Consumers read
	// this instead of scattering individual Timer:: calls, so unscaled (real) and
	// scaled (gameplay) clocks are sampled coherently from a single point.
	struct FrameTime
	{
		float  unscaledDelta;   // Timer::getDeltaTime
		double unscaledElapsed; // Timer::getElapsedTime
		float  scaledDelta;     // Timer::getGameDeltaTime
		double scaledElapsed;   // Timer::getScaledElapsedTime
		float  timeScale;       // Timer::getTimeScale
		bool   frozen;          // Timer::isFrozen

		static FrameTime current()
		{
			return FrameTime{
				static_cast<float>(Timer::getDeltaTime()),     Timer::getElapsedTime(),
				static_cast<float>(Timer::getGameDeltaTime()), Timer::getScaledElapsedTime(),
				static_cast<float>(Timer::getTimeScale()),     Timer::isFrozen() };
		}
	};
}
