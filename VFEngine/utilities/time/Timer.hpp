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

		// Gameplay time state (Phase 2: pause / step / time-scale). Driven by the
		// editor mode service; the renderer/UI keep using the raw deltaTime above so
		// editor camera/UI/render pacing is never affected by slow-mo or pause.
		inline static double timeScale = 1.0;
		inline static bool gamePaused = false;
		inline static bool stepRequested = false;
		inline static double gameDeltaTime = 0.0;

		// Hard freeze (VK-992): halts gameplay time independently of gamePaused /
		// timeScale. When set, computeGameDelta() returns 0 regardless of scale.
		inline static bool gameFrozen = false;
		// Accumulated gameplay clock (sum of gameDeltaTime). Feeds scaled shader time
		// (water/VFX) so slow-mo/freeze are visible without retroactively rescaling
		// past time the way rawElapsed * currentScale would.
		inline static double scaledElapsedTime = 0.0;

	public:
		// Pure arithmetic for the gameplay delta, factored out of update() so it can
		// be unit-tested deterministically (update() reads the wall clock). Public so
		// the CPU test suite can exercise it directly.
		static double computeGameDelta(double rawDelta, double scale, bool paused, bool frozen, bool& step);

		static void initialize();
		static void update();
		static double getDeltaTime();
		static double getElapsedTime();
		static double getFPS();
		static void reset();

		// Gameplay time accessors (Phase 2).
		static double getGameDeltaTime();
		static bool isGameTimeActive();
		static void setTimeScale(double scale);
		static void setGamePaused(bool paused);
		static void requestStep();
		static void resetGameTime();

		// Scaled-time accessors (VK-992).
		static double getTimeScale();
		static double getScaledElapsedTime();
		static void freeze();
		static void unfreeze();
		static bool isFrozen();
	};
}


