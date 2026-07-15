#include <doctest.h>
#include <audio/BusMetering.hpp>

#include <limits>
#include <vector>

namespace
{
    using core::audio::metering::BusNode;

    std::vector<float> meter(const std::vector<BusNode>& nodes,
                             const std::vector<float>& directPower)
    {
        std::vector<float> out(nodes.size(), 0.0f);
        core::audio::metering::accumulateBusRms(nodes, directPower, out);
        return out;
    }
}

TEST_SUITE("AudioBusMetering")
{
    TEST_CASE("master-only source applies its fader")
    {
        const auto out = meter({BusNode{0, 0.5f, 0.5f, false, false}}, {1.0f});
        REQUIRE(out.size() == 1);
        CHECK(out[0] == doctest::Approx(0.5f));
    }

    TEST_CASE("child feeds Master and Master remains post-own-fader")
    {
        std::vector<BusNode> nodes{
            BusNode{0, 0.5f, 0.5f, false, false},
            BusNode{0, 0.25f, 0.125f, false, false}};
        const auto out = meter(nodes, {0.0f, 1.0f});
        CHECK(out[1] == doctest::Approx(0.25f));
        CHECK(out[0] == doctest::Approx(0.125f));
    }

    TEST_CASE("changing Master leaves the child meter unchanged")
    {
        std::vector<BusNode> nodes{
            BusNode{0, 1.0f, 1.0f, false, false},
            BusNode{0, 0.5f, 0.5f, false, false}};
        const auto full = meter(nodes, {0.0f, 1.0f});
        nodes[0].volume = 0.5f;
        const auto half = meter(nodes, {0.0f, 1.0f});
        CHECK(half[1] == doctest::Approx(full[1]));
        CHECK(half[0] == doctest::Approx(full[0] * 0.5f));
    }

    TEST_CASE("four uncorrelated equal sources power-sum to twice one source")
    {
        const auto out = meter({BusNode{}}, {4.0f * 0.25f * 0.25f});
        CHECK(out[0] == doctest::Approx(0.5f));
    }

    TEST_CASE("muting an ancestor does not zero a pre-parent child meter")
    {
        const auto out = meter(
            {BusNode{0, 1.0f, 0.0f, true, false}, BusNode{0, 1.0f, 0.0f, false, false}},
            {0.0f, 1.0f});
        CHECK(out[1] == doctest::Approx(1.0f));
        CHECK(out[0] == doctest::Approx(0.0f));
    }

    TEST_CASE("soloed child remains audible in its ancestors using current effective gain")
    {
        const auto out = meter(
            {BusNode{0, 0.25f, 0.0f, false, false}, BusNode{0, 0.5f, 0.5f, false, true}},
            {1.0f, 1.0f});
        CHECK(out[1] == doctest::Approx(0.5f));
        CHECK(out[0] == doctest::Approx(0.5f));
    }

    TEST_CASE("soloed parent faders remain pre-parent while orphan edges bypass")
    {
        const auto bothSoloed = meter(
            {BusNode{0, 0.25f, 0.25f, false, true},
             BusNode{0, 0.5f, 0.125f, false, true}},
            {0.0f, 1.0f});
        CHECK(bothSoloed[1] == doctest::Approx(0.5f));
        CHECK(bothSoloed[0] == doctest::Approx(0.125f));

        const auto nestedOrphan = meter(
            {BusNode{0, 0.25f, 0.0f, false, false},
             BusNode{0, 0.5f, 0.5f, false, true},
             BusNode{1, 0.5f, 0.25f, false, true}},
            {0.0f, 0.0f, 1.0f});
        CHECK(nestedOrphan[2] == doctest::Approx(0.5f));
        CHECK(nestedOrphan[1] == doctest::Approx(0.25f));
        CHECK(nestedOrphan[0] == doctest::Approx(0.25f));
    }

    TEST_CASE("deep chain does not double-count and cycles terminate")
    {
        const auto chain = meter(
            {BusNode{0}, BusNode{0}, BusNode{1}}, {0.0f, 0.0f, 1.0f});
        CHECK(chain[0] == doctest::Approx(1.0f));
        CHECK(chain[1] == doctest::Approx(1.0f));
        CHECK(chain[2] == doctest::Approx(1.0f));

        const auto cycle = meter(
            {BusNode{1}, BusNode{0}}, {1.0f, 0.0f});
        CHECK(cycle[0] == doctest::Approx(1.0f));
        CHECK(cycle[1] == doctest::Approx(1.0f));
    }

    TEST_CASE("traversal guard meters exactly 32 nodes of an over-deep chain")
    {
        constexpr std::size_t nodeCount =
            core::audio::metering::kMaxTraversalDepth + 1;
        std::vector<BusNode> nodes(nodeCount);
        for (std::size_t i = 1; i < nodes.size(); ++i)
            nodes[i].parentId = static_cast<uint32_t>(i - 1);

        std::vector<float> directPower(nodeCount, 0.0f);
        directPower.back() = 1.0f;
        const auto out = meter(nodes, directPower);

        CHECK(out.front() == 0.0f);
        CHECK(out[1] == doctest::Approx(1.0f));
        CHECK(out.back() == doctest::Approx(1.0f));
    }

    TEST_CASE("invalid parents and non-finite values do not poison other buses")
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto out = meter(
            {BusNode{99, nan, nan, false, false}, BusNode{1}}, {nan, 1.0f});
        CHECK(out[0] == 0.0f);
        CHECK(out[1] == doctest::Approx(1.0f));

        const auto saturated = meter(
            {BusNode{0, std::numeric_limits<float>::max(), 1.0f, false, false}},
            {std::numeric_limits<float>::max()});
        CHECK(std::isfinite(saturated[0]));
        CHECK(saturated[0] > 0.0f);
    }

    TEST_CASE("peak hold rises immediately and decays safely")
    {
        using core::audio::metering::decayPeakHold;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(decayPeakHold(0.2f, 0.8f, 0.1f) == doctest::Approx(0.8f));
        CHECK(decayPeakHold(1.0f, 0.2f, 0.5f) == doctest::Approx(0.7f));
        CHECK(decayPeakHold(0.1f, 0.2f, 10.0f) == doctest::Approx(0.2f));
        CHECK(decayPeakHold(1.0f, 0.0f, -1.0f) == doctest::Approx(1.0f));
        CHECK(decayPeakHold(nan, 0.2f, 0.1f) == doctest::Approx(0.2f));
    }
}
