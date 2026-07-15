#pragma once

// VK-1519: pure, device-free sidechain ducking policy. The audio thread feeds it an
// estimated bus RMS and folds the returned gain into AudioBusManager's existing effective
// volume path. Keeping the DSP here header-only lets the CPU-only Tests target exercise it
// without linking Audio or OpenAL.

#include "types/AudioTypes.hpp"

#include <algorithm>
#include <cmath>

namespace core::audio::ducking
{
    inline constexpr float kSilenceDb = -120.0f;
    inline constexpr float kReductionSnapEpsilonDb = 1e-4f;
    inline constexpr float kGainDirtyEpsilon = 1e-5f;

    [[nodiscard]] inline float linearToDb(float amplitude) noexcept
    {
        if (!std::isfinite(amplitude) || amplitude <= 0.0f)
        {
            return kSilenceDb;
        }
        return 20.0f * std::log10(amplitude);
    }

    [[nodiscard]] inline float dbToLinear(float decibels) noexcept
    {
        if (!std::isfinite(decibels))
        {
            return 1.0f;
        }
        return std::pow(10.0f, decibels / 20.0f);
    }

    [[nodiscard]] inline types::BusDuckConfig sanitizeConfig(
        const types::BusDuckConfig& config)
    {
        types::BusDuckConfig sanitized = config;
        sanitized.thresholdDb = std::isfinite(config.thresholdDb)
            ? std::clamp(config.thresholdDb,
                         types::kBusDuckMinThresholdDb,
                         types::kBusDuckMaxThresholdDb)
            : types::BusDuckConfig{}.thresholdDb;
        sanitized.amountDb = std::isfinite(config.amountDb)
            ? std::clamp(config.amountDb,
                         types::kBusDuckMinAmountDb,
                         types::kBusDuckMaxAmountDb)
            : types::BusDuckConfig{}.amountDb;
        sanitized.attackMs = std::isfinite(config.attackMs)
            ? std::clamp(config.attackMs,
                         types::kBusDuckMinTimeMs,
                         types::kBusDuckMaxTimeMs)
            : types::BusDuckConfig{}.attackMs;
        sanitized.releaseMs = std::isfinite(config.releaseMs)
            ? std::clamp(config.releaseMs,
                         types::kBusDuckMinTimeMs,
                         types::kBusDuckMaxTimeMs)
            : types::BusDuckConfig{}.releaseMs;
        return sanitized;
    }

    class DuckEnvelope
    {
    public:
        [[nodiscard]] float update(float sourceRms,
                                   const types::BusDuckConfig& config,
                                   float deltaTime) noexcept
        {
            const float levelDb = linearToDb(sourceRms);
            const float targetReductionDb = levelDb >= config.thresholdDb
                ? config.amountDb : 0.0f;
            const float timeMs = targetReductionDb > reductionDb
                ? config.attackMs : config.releaseMs;
            advance(targetReductionDb, timeMs, deltaTime);
            return gain();
        }

        [[nodiscard]] float release(float releaseMs, float deltaTime) noexcept
        {
            advance(0.0f, releaseMs, deltaTime);
            return gain();
        }

        void reset() noexcept
        {
            reductionDb = 0.0f;
        }

        [[nodiscard]] float gain() const noexcept
        {
            return dbToLinear(-reductionDb);
        }

        [[nodiscard]] float reduction() const noexcept
        {
            return reductionDb;
        }

        [[nodiscard]] bool isUnity() const noexcept
        {
            return reductionDb == 0.0f;
        }

    private:
        void advance(float targetReductionDb, float timeMs, float deltaTime) noexcept
        {
            targetReductionDb = std::isfinite(targetReductionDb)
                ? std::clamp(targetReductionDb,
                             types::kBusDuckMinAmountDb,
                             types::kBusDuckMaxAmountDb)
                : 0.0f;

            if (!std::isfinite(deltaTime) || deltaTime <= 0.0f)
            {
                return;
            }
            if (!std::isfinite(timeMs) || timeMs <= 0.0f)
            {
                reductionDb = targetReductionDb;
                return;
            }

            const float tauSeconds = timeMs * 0.001f;
            const float decay = std::exp(-deltaTime / tauSeconds);
            const float next = targetReductionDb
                + (reductionDb - targetReductionDb) * decay;
            if (std::abs(next - targetReductionDb) <= kReductionSnapEpsilonDb)
            {
                reductionDb = targetReductionDb;
                return;
            }

            // Do not clamp to the current config amount: lowering amountDb should release
            // smoothly from the old reduction instead of jumping upward in level.
            reductionDb = std::clamp(next,
                                     types::kBusDuckMinAmountDb,
                                     types::kBusDuckMaxAmountDb);
        }

        float reductionDb = 0.0f;
    };
}
