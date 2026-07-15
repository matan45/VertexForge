#include <doctest.h>
#include <audio/StreamingMetering.hpp>

#include <vector>

TEST_SUITE("AudioStreamingMetering")
{
    TEST_CASE("queue offset selects the audible chunk and local envelope window")
    {
        const std::vector<core::audio::QueuedEnvelopeChunk> chunks{
            {1, 0, 100, {255, 1}},
            {2, 100, 100, {1, 255}}};

        const auto first = core::audio::sampleQueuedPlayback(chunks, 0, 1, 100);
        CHECK(first.positionSeconds == doctest::Approx(0.0f));
        CHECK(first.rms == doctest::Approx(1.0f));

        const auto boundary = core::audio::sampleQueuedPlayback(chunks, 100, 1, 100);
        CHECK(boundary.positionSeconds == doctest::Approx(1.0f));
        CHECK(boundary.rms == doctest::Approx(resource::decodeEnvelopeDb(1)));
    }

    TEST_CASE("loop head queued behind old tail keeps asset positions distinct")
    {
        const std::vector<core::audio::QueuedEnvelopeChunk> chunks{
            {3, 900, 100, {1}},
            {1, 0, 100, {255}}};

        const auto tail = core::audio::sampleQueuedPlayback(chunks, 50, 1, 100);
        CHECK(tail.positionSeconds == doctest::Approx(9.5f));
        CHECK(tail.rms == doctest::Approx(resource::decodeEnvelopeDb(1)));

        const auto head = core::audio::sampleQueuedPlayback(chunks, 100, 1, 100);
        CHECK(head.positionSeconds == doctest::Approx(0.0f));
        CHECK(head.rms == doctest::Approx(1.0f));
    }

    TEST_CASE("queue sampling handles stereo units, final boundaries, and invalid formats")
    {
        const std::vector<core::audio::QueuedEnvelopeChunk> chunks{
            {1, 200, 50, {255}}};
        const auto inside = core::audio::sampleQueuedPlayback(chunks, 20, 2, 100);
        CHECK(inside.positionSeconds == doctest::Approx(1.1f));

        const auto past = core::audio::sampleQueuedPlayback(chunks, 999, 2, 100);
        CHECK(past.positionSeconds == doctest::Approx(1.25f));
        CHECK(past.rms == 0.0f);

        CHECK(core::audio::sampleQueuedPlayback({}, 0, 1, 100).rms == 0.0f);
        CHECK(core::audio::sampleQueuedPlayback(chunks, 0, 0, 100).rms == 0.0f);
        CHECK(core::audio::sampleQueuedPlayback(chunks, 0, 1, 0).rms == 0.0f);
    }
}
