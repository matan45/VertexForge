#include <doctest.h>
#include <audio/BusDucking.hpp>

#include <cmath>
#include <limits>

TEST_SUITE("AudioBusDucking")
{
    using core::audio::ducking::DuckEnvelope;
    using core::audio::ducking::dbToLinear;
    using core::audio::ducking::linearToDb;

    TEST_CASE("amplitude and decibel conversions are safe")
    {
        CHECK(dbToLinear(0.0f) == doctest::Approx(1.0f));
        CHECK(dbToLinear(-6.0206f) == doctest::Approx(0.5f).epsilon(0.0001));
        CHECK(linearToDb(0.5f) == doctest::Approx(-6.0206f).epsilon(0.0001));
        CHECK(linearToDb(0.0f) == core::audio::ducking::kSilenceDb);
        CHECK(linearToDb(std::numeric_limits<float>::quiet_NaN())
              == core::audio::ducking::kSilenceDb);
        CHECK(dbToLinear(std::numeric_limits<float>::quiet_NaN()) == 1.0f);
    }

    TEST_CASE("threshold applies a fixed positive reduction")
    {
        types::BusDuckConfig config;
        config.thresholdDb = -30.0f;
        config.amountDb = 12.0f;
        config.attackMs = 0.0f;

        DuckEnvelope envelope;
        CHECK(envelope.update(dbToLinear(-30.1f), config, 0.01f) == 1.0f);
        CHECK(envelope.update(dbToLinear(-30.0f), config, 0.01f)
              == doctest::Approx(dbToLinear(-12.0f)));

        envelope.reset();
        config.amountDb = 0.0f;
        CHECK(envelope.update(1.0f, config, 0.01f) == 1.0f);
    }

    TEST_CASE("one attack time is one exponential time constant")
    {
        types::BusDuckConfig config;
        config.thresholdDb = -30.0f;
        config.amountDb = 12.0f;
        config.attackMs = 100.0f;

        DuckEnvelope envelope;
        envelope.update(1.0f, config, 0.1f);
        const float expectedReduction = config.amountDb * (1.0f - std::exp(-1.0f));
        CHECK(envelope.reduction() == doctest::Approx(expectedReduction).epsilon(0.0001));
    }

    TEST_CASE("attack and release are monotonic and do not overshoot")
    {
        types::BusDuckConfig config;
        config.amountDb = 18.0f;
        config.attackMs = 80.0f;
        config.releaseMs = 200.0f;

        DuckEnvelope envelope;
        float previous = envelope.reduction();
        for (int i = 0; i < 20; ++i)
        {
            envelope.update(1.0f, config, 0.01f);
            CHECK(envelope.reduction() >= previous);
            CHECK(envelope.reduction() <= config.amountDb);
            previous = envelope.reduction();
        }

        for (int i = 0; i < 1000 && !envelope.isUnity(); ++i)
        {
            previous = envelope.reduction();
            envelope.update(0.0f, config, 0.01f);
            CHECK(envelope.reduction() <= previous);
            CHECK(envelope.reduction() >= 0.0f);
        }
        CHECK(envelope.isUnity());
        CHECK(envelope.gain() == 1.0f);
    }

    TEST_CASE("time partitioning produces the same envelope")
    {
        types::BusDuckConfig config;
        config.amountDb = 20.0f;
        config.attackMs = 250.0f;

        DuckEnvelope oneStep;
        DuckEnvelope tenSteps;
        oneStep.update(1.0f, config, 0.1f);
        for (int i = 0; i < 10; ++i)
        {
            tenSteps.update(1.0f, config, 0.01f);
        }
        CHECK(tenSteps.reduction() == doctest::Approx(oneStep.reduction()).epsilon(0.0001));
    }

    TEST_CASE("zero times snap and invalid delta time holds")
    {
        types::BusDuckConfig config;
        config.amountDb = 24.0f;
        config.attackMs = 0.0f;
        config.releaseMs = 0.0f;

        DuckEnvelope envelope;
        CHECK(envelope.update(1.0f, config, 0.01f)
              == doctest::Approx(dbToLinear(-24.0f)));
        CHECK(envelope.update(0.0f, config, 0.01f) == 1.0f);

        envelope.update(1.0f, config, 0.01f);
        const float held = envelope.reduction();
        envelope.update(0.0f, config, -1.0f);
        CHECK(envelope.reduction() == held);
        envelope.update(0.0f, config, std::numeric_limits<float>::quiet_NaN());
        CHECK(envelope.reduction() == held);
    }

    TEST_CASE("removal release reaches exact unity")
    {
        types::BusDuckConfig config;
        config.amountDb = 12.0f;
        config.attackMs = 0.0f;

        DuckEnvelope envelope;
        envelope.update(1.0f, config, 0.01f);
        CHECK(envelope.gain() < 1.0f);

        for (int i = 0; i < 1000 && !envelope.isUnity(); ++i)
        {
            envelope.release(100.0f, 0.01f);
        }
        CHECK(envelope.isUnity());
        CHECK(envelope.gain() == 1.0f);
    }

    TEST_CASE("backend sanitization clamps authored values")
    {
        types::BusDuckConfig config;
        config.thresholdDb = -100.0f;
        config.amountDb = 100.0f;
        config.attackMs = -1.0f;
        config.releaseMs = std::numeric_limits<float>::infinity();

        const auto sanitized = core::audio::ducking::sanitizeConfig(config);
        CHECK(sanitized.thresholdDb == types::kBusDuckMinThresholdDb);
        CHECK(sanitized.amountDb == types::kBusDuckMaxAmountDb);
        CHECK(sanitized.attackMs == types::kBusDuckMinTimeMs);
        CHECK(sanitized.releaseMs == types::BusDuckConfig{}.releaseMs);
    }
}
