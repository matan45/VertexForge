#pragma once

// VK-1514: AL-free queue-offset math shared by StreamingAudioSource and CPU tests.

#include "resource/AudioAnalysis.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace core::audio
{
    struct QueuedEnvelopeChunk
    {
        uint32_t bufferId = 0;
        std::size_t startSample = 0; // interleaved sample index in the asset
        std::size_t sampleCount = 0;
        std::vector<uint8_t> rms;
    };

    struct StreamingPlaybackMetrics
    {
        float positionSeconds = 0.0f;
        float rms = 0.0f;
    };

    inline StreamingPlaybackMetrics sampleQueuedPlayback(
        std::span<const QueuedEnvelopeChunk> chunks,
        std::size_t queueSampleOffset,
        uint32_t channels,
        uint32_t sampleRate)
    {
        StreamingPlaybackMetrics result;
        if (chunks.empty() || channels == 0 || sampleRate == 0)
            return result;

        std::size_t remaining = queueSampleOffset;
        for (const QueuedEnvelopeChunk& chunk : chunks)
        {
            if (remaining < chunk.sampleCount)
            {
                const double samplesPerSecond = static_cast<double>(sampleRate)
                    * static_cast<double>(channels);
                result.positionSeconds = static_cast<float>(
                    static_cast<double>(chunk.startSample + remaining) / samplesPerSecond);
                result.rms = resource::sampleEnvelope(
                    chunk.rms, static_cast<float>(static_cast<double>(remaining) / samplesPerSecond));
                return result;
            }
            remaining -= chunk.sampleCount;
        }

        const QueuedEnvelopeChunk& last = chunks.back();
        result.positionSeconds = static_cast<float>(
            static_cast<double>(last.startSample + last.sampleCount)
            / (static_cast<double>(sampleRate) * static_cast<double>(channels)));
        return result;
    }
}
