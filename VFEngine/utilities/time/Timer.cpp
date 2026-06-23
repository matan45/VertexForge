#include "Timer.hpp"
namespace engineTime {
	void Timer::initialize()
	{
		startTime = Clock::now();
		lastTime = startTime;
		currentTime = startTime;
		deltaTime = 0.0;
	}

	void Timer::update()
	{
		currentTime = Clock::now();
		Duration frameTime = std::chrono::duration_cast<Duration>(currentTime - lastTime);
		deltaTime = frameTime.count();  // Get deltaTime in seconds
		lastTime = currentTime;

		// Derive the gameplay delta from the raw frame delta (Phase 2).
		gameDeltaTime = computeGameDelta(deltaTime, timeScale, gamePaused, gameFrozen, stepRequested);

		// Advance the scaled gameplay clock by this frame's gameplay delta (VK-992).
		scaledElapsedTime += gameDeltaTime;
	}

	double Timer::computeGameDelta(double rawDelta, double scale, bool paused, bool frozen, bool& step)
	{
		if (frozen)
		{
			return 0.0;
		}
		if (!paused)
		{
			return rawDelta * scale;
		}
		if (step)
		{
			// Reproducible single-step advance, independent of the editor's frame rate.
			step = false;
			return (1.0 / 60.0) * scale;
		}
		return 0.0;
	}

	double Timer::getDeltaTime()
	{
		return deltaTime;
	}

	double Timer::getElapsedTime()
	{
		Duration elapsed = std::chrono::duration_cast<Duration>(currentTime - startTime);
		return elapsed.count();  // Return time in seconds
	}

	double Timer::getFPS()
	{
		if (deltaTime > 0) {
			return 1.0 / deltaTime;
		}
		return 0.0;
	}

	void Timer::reset()
	{
		startTime = Clock::now();
		lastTime = startTime;
		currentTime = startTime;
	}

	double Timer::getGameDeltaTime()
	{
		return gameDeltaTime;
	}

	bool Timer::isGameTimeActive()
	{
		return gameDeltaTime > 0.0;
	}

	void Timer::setTimeScale(double scale)
	{
		timeScale = scale;
	}

	void Timer::setGamePaused(bool paused)
	{
		gamePaused = paused;
	}

	void Timer::requestStep()
	{
		stepRequested = true;
	}

	void Timer::resetGameTime()
	{
		timeScale = 1.0;
		gamePaused = false;
		stepRequested = false;
		gameDeltaTime = 0.0;
		gameFrozen = false;
		scaledElapsedTime = 0.0;
	}

	double Timer::getTimeScale()
	{
		return timeScale;
	}

	double Timer::getScaledElapsedTime()
	{
		return scaledElapsedTime;
	}

	void Timer::freeze()
	{
		gameFrozen = true;
	}

	void Timer::unfreeze()
	{
		gameFrozen = false;
	}

	bool Timer::isFrozen()
	{
		return gameFrozen;
	}

}
