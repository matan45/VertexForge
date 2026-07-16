#include <doctest.h>
#include <resource/AudioDownmix.hpp>
#include <vector>

// ============================================================
// VK-1507: import-time force-mono downmix. resource::downmixToMono averages
// interleaved int16 PCM channels into a single mono channel so stereo clips
// destined for 3D audio sources spatialize (OpenAL spatializes mono only).
//
// CPU-only: no OpenAL device, no import module — the helper is header-only.
// ============================================================

TEST_SUITE("AudioDownmix")
{
    TEST_CASE("mono passthrough: channels == 1 returns a copy")
    {
        const std::vector<short> in{1, 2, 3, -4, 5};
        const std::vector<short> out = resource::downmixToMono(in, 1);
        CHECK(out == in);
    }

    TEST_CASE("stereo: averages interleaved L/R per frame")
    {
        // {L0,R0, L1,R1} -> {(10+20)/2, (30+40)/2} = {15, 35}
        const std::vector<short> in{10, 20, 30, 40};
        const std::vector<short> out = resource::downmixToMono(in, 2);
        REQUIRE(out.size() == 2);
        CHECK(out[0] == 15);
        CHECK(out[1] == 35);
    }

    TEST_CASE("clamping / extreme values stay in int16 range")
    {
        // Both channels at the negative rail -> stays at the rail.
        CHECK(resource::downmixToMono(std::vector<short>{-32768, -32768}, 2)[0] == -32768);

        // Both channels at the positive rail -> stays at the rail.
        CHECK(resource::downmixToMono(std::vector<short>{32767, 32767}, 2)[0] == 32767);

        // Opposite rails: (32767 + -32768) / 2 = -1 / 2 = 0 (integer truncation).
        CHECK(resource::downmixToMono(std::vector<short>{32767, -32768}, 2)[0] == 0);
    }

    TEST_CASE("multi-channel (5.1-style): averages all channels per frame")
    {
        // 3 channels, two frames: {10,20,30} -> 20, {40,50,60} -> 50
        const std::vector<short> in{10, 20, 30, 40, 50, 60};
        const std::vector<short> out = resource::downmixToMono(in, 3);
        REQUIRE(out.size() == 2);
        CHECK(out[0] == 20);
        CHECK(out[1] == 50);
    }

    TEST_CASE("size not a multiple of channels: trailing partial frame dropped")
    {
        // 3 samples, 2 channels -> 1 whole frame (avg of first two); last dropped.
        const std::vector<short> in{4, 8, 99};
        const std::vector<short> out = resource::downmixToMono(in, 2);
        REQUIRE(out.size() == 1);
        CHECK(out[0] == 6);
    }

    TEST_CASE("degenerate inputs are guarded")
    {
        // Empty input -> empty output.
        CHECK(resource::downmixToMono(std::vector<short>{}, 2).empty());

        // channels == 0 -> empty (no divide-by-zero).
        CHECK(resource::downmixToMono(std::vector<short>{1, 2, 3}, 0).empty());
    }
}
