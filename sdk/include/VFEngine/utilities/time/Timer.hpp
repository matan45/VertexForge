#pragma once
#include "Clock.hpp"

namespace engineTime {

	class Timer
	{
	private:
		// Static variables for time tracking
		inline static TimePoint startTime;
		inline static TimePoint lastTime;
		inline static TimePoint currentTime;
		inline static double deltaTime;

	public:
		static void initialize();
		static void update();
		static double getDeltaTime();
		static double getElapsedTime();
		static double getFPS();
		static void reset();
	};
}


