#include <doctest.h>

#include "impl/time/TimeServiceImpl.hpp"
#include "time/Timer.hpp"
#include "time/FrameTime.hpp"

// VK-992: TimeServiceImpl is the Services-layer authority for global gameplay time
// scale and hard freeze. These tests exercise the CPU-only paths (no graphics, no
// dispatcher needed): the static clamp helper plus the direct method calls that
// write the engineTime::Timer authority. Timer keeps global static state, so each
// case resets it to avoid cross-test contamination.

using services::TimeServiceImpl;
using engineTime::Timer;
using engineTime::FrameTime;

TEST_CASE("TimeServiceImpl::clampTimeScale clamps to [0, 8]")
{
    SUBCASE("below the floor clamps to 0")
    {
        CHECK(TimeServiceImpl::clampTimeScale(-1.0f) == doctest::Approx(0.0f));
        CHECK(TimeServiceImpl::clampTimeScale(-0.001f) == doctest::Approx(0.0f));
    }

    SUBCASE("above the ceiling clamps to 8")
    {
        CHECK(TimeServiceImpl::clampTimeScale(8.0001f) == doctest::Approx(8.0f));
        CHECK(TimeServiceImpl::clampTimeScale(100.0f) == doctest::Approx(8.0f));
    }

    SUBCASE("in-range values pass through unchanged")
    {
        CHECK(TimeServiceImpl::clampTimeScale(0.0f) == doctest::Approx(0.0f));
        CHECK(TimeServiceImpl::clampTimeScale(0.2f) == doctest::Approx(0.2f));
        CHECK(TimeServiceImpl::clampTimeScale(1.0f) == doctest::Approx(1.0f));
        CHECK(TimeServiceImpl::clampTimeScale(4.0f) == doctest::Approx(4.0f));
        CHECK(TimeServiceImpl::clampTimeScale(8.0f) == doctest::Approx(8.0f));
    }
}

TEST_CASE("TimeServiceImpl::setGlobalTimeScale writes the clamped value to Timer")
{
    Timer::resetGameTime();
    TimeServiceImpl service;

    SUBCASE("in-range scale is written verbatim")
    {
        service.setGlobalTimeScale(0.25f);
        CHECK(Timer::getTimeScale() == doctest::Approx(0.25));

        service.setGlobalTimeScale(4.0f);
        CHECK(Timer::getTimeScale() == doctest::Approx(4.0));
    }

    SUBCASE("below-range scale is clamped to 0 before writing Timer")
    {
        service.setGlobalTimeScale(-2.0f);
        CHECK(Timer::getTimeScale() == doctest::Approx(0.0));
    }

    SUBCASE("above-range scale is clamped to 8 before writing Timer")
    {
        service.setGlobalTimeScale(50.0f);
        CHECK(Timer::getTimeScale() == doctest::Approx(8.0));
    }

    Timer::resetGameTime();
}

TEST_CASE("TimeServiceImpl freeze / unfreeze drive Timer::isFrozen")
{
    Timer::resetGameTime();
    TimeServiceImpl service;

    CHECK(Timer::isFrozen() == false);

    service.freeze();
    CHECK(Timer::isFrozen() == true);

    service.unfreeze();
    CHECK(Timer::isFrozen() == false);

    Timer::resetGameTime();
}

TEST_CASE("TimeServiceImpl::getTimeSnapshot reflects current Timer state")
{
    Timer::resetGameTime();
    TimeServiceImpl service;

    SUBCASE("snapshot timeScale tracks a setGlobalTimeScale call")
    {
        service.setGlobalTimeScale(2.5f);
        FrameTime snapshot = service.getTimeSnapshot();
        CHECK(snapshot.timeScale == doctest::Approx(2.5f));
        CHECK(snapshot.frozen == false);
    }

    SUBCASE("snapshot frozen flag tracks freeze / unfreeze")
    {
        service.freeze();
        CHECK(service.getTimeSnapshot().frozen == true);

        service.unfreeze();
        CHECK(service.getTimeSnapshot().frozen == false);
    }

    SUBCASE("clamped scale also shows up in the snapshot")
    {
        service.setGlobalTimeScale(20.0f); // clamps to 8
        FrameTime snapshot = service.getTimeSnapshot();
        CHECK(snapshot.timeScale == doctest::Approx(8.0f));
    }

    Timer::resetGameTime();
}
