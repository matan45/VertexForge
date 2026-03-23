#include "HeightmapGenerator.hpp"
#include "../noise/PerlinNoise.hpp"
#include "../noise/SimplexNoise.hpp"
#include "../writer/VFImageWriter.hpp"
#include <cmath>
#include <algorithm>
#include <memory>

namespace procedural
{
    namespace
    {
        // Create noise evaluator based on type
        std::unique_ptr<PerlinNoise> createPerlin(uint32_t seed)
        {
            return std::make_unique<PerlinNoise>(seed);
        }

        std::unique_ptr<SimplexNoise> createSimplex(uint32_t seed)
        {
            return std::make_unique<SimplexNoise>(seed);
        }

        // Evaluate single noise sample using either Perlin or Simplex
        float evaluateNoise(const PerlinNoise* perlin, const SimplexNoise* simplex,
                           NoiseType type, float x, float y)
        {
            if (type == NoiseType::Simplex && simplex)
                return simplex->evaluate(x, y);
            return perlin->evaluate(x, y);
        }

        // Per-octave offsets to break alignment between octaves
        // Each octave samples from a different region, preventing repetitive patterns
        constexpr float octaveOffsets[][2] = {
            {   0.0f,    0.0f  },
            { 137.2f,  251.7f  },
            { -83.4f,  419.3f  },
            { 312.8f, -147.6f  },
            {-201.5f, -309.1f  },
            { 467.3f,  178.9f  },
            {-156.7f,  523.4f  },
            { 289.1f, -412.8f  },
            {-378.6f,  167.2f  },
            { 534.9f, -283.5f  },
            {-423.1f, -198.7f  },
            { 195.4f,  376.8f  },
            {-267.3f,  491.2f  },
            { 412.6f,  -56.9f  },
            {-149.8f, -467.3f  },
            { 356.2f,  234.1f  },
        };

        // FBM (Fractal Brownian Motion)
        float fbm(const PerlinNoise* perlin, const SimplexNoise* simplex,
                  NoiseType type, float x, float y,
                  int octaves, float frequency, float lacunarity, float persistence)
        {
            float sum = 0.0f;
            float amp = 1.0f;
            float freq = frequency;
            float maxAmp = 0.0f;

            for (int i = 0; i < octaves; ++i)
            {
                float ox = x * freq + octaveOffsets[i & 15][0];
                float oy = y * freq + octaveOffsets[i & 15][1];
                sum += evaluateNoise(perlin, simplex, type, ox, oy) * amp;
                maxAmp += amp;
                amp *= persistence;
                freq *= lacunarity;
            }

            return sum / maxAmp; // Normalize to [-1, 1]
        }

        // Ridged noise
        float ridged(const PerlinNoise* perlin, const SimplexNoise* simplex,
                     NoiseType type, float x, float y,
                     int octaves, float frequency, float lacunarity, float gain)
        {
            float sum = 0.0f;
            float amp = 1.0f;
            float freq = frequency;
            float weight = 1.0f;

            for (int i = 0; i < octaves; ++i)
            {
                float ox = x * freq + octaveOffsets[i & 15][0];
                float oy = y * freq + octaveOffsets[i & 15][1];
                float n = evaluateNoise(perlin, simplex, type, ox, oy);
                n = 1.0f - std::abs(n); // Ridge
                n *= n;                   // Square for sharper ridges
                n *= weight;
                weight = std::clamp(n * 2.0f, 0.0f, 1.0f);
                sum += n * amp;
                amp *= gain;
                freq *= lacunarity;
            }

            return sum; // Already in [0, ~1] range
        }

        // Billowy noise (smooth rounded valleys — complement of Ridged)
        float billowy(const PerlinNoise* perlin, const SimplexNoise* simplex,
                      NoiseType type, float x, float y,
                      int octaves, float frequency, float lacunarity, float persistence)
        {
            float sum = 0.0f;
            float amp = 1.0f;
            float freq = frequency;
            float maxAmp = 0.0f;

            for (int i = 0; i < octaves; ++i)
            {
                float ox = x * freq + octaveOffsets[i & 15][0];
                float oy = y * freq + octaveOffsets[i & 15][1];
                float n = evaluateNoise(perlin, simplex, type, ox, oy);
                n = std::abs(n); // Smooth rounded valleys
                sum += n * amp;
                maxAmp += amp;
                amp *= persistence;
                freq *= lacunarity;
            }

            return sum / maxAmp; // Normalize to [0, 1]
        }

        // Domain warping
        void domainWarp(const PerlinNoise* perlin, const SimplexNoise* simplex,
                        NoiseType type, float& x, float& y,
                        float warpAmp, float warpFreq)
        {
            float offsetX = fbm(perlin, simplex, type, x, y, 4, warpFreq, 2.0f, 0.5f);
            float offsetY = fbm(perlin, simplex, type, x + 5.2f, y + 1.3f, 4, warpFreq, 2.0f, 0.5f);
            x += offsetX * warpAmp;
            y += offsetY * warpAmp;
        }

        // Core generation logic
        HeightmapResult generateInternal(const HeightmapParams& params,
                                         uint32_t outWidth, uint32_t outHeight,
                                         ProgressCallback progress)
        {
            auto perlin = createPerlin(params.seed);
            auto simplex = createSimplex(params.seed);

            HeightmapResult result;
            result.width = outWidth;
            result.height = outHeight;
            result.rgbaData.resize(static_cast<size_t>(outWidth) * outHeight * 4);

            // Compute scale factor if generating preview
            float scaleX = static_cast<float>(params.width) / static_cast<float>(outWidth);
            float scaleY = static_cast<float>(params.height) / static_cast<float>(outHeight);

            // First pass: compute raw heights to find min/max for normalization
            std::vector<float> rawHeights(static_cast<size_t>(outWidth) * outHeight);
            float minH = std::numeric_limits<float>::max();
            float maxH = std::numeric_limits<float>::lowest();

            for (uint32_t y = 0; y < outHeight; ++y)
            {
                for (uint32_t x = 0; x < outWidth; ++x)
                {
                    float sx = static_cast<float>(x) * scaleX;
                    float sy = static_cast<float>(y) * scaleY;

                    // Apply domain warping
                    if (params.domainWarp.enabled)
                    {
                        domainWarp(perlin.get(), simplex.get(), params.noiseType,
                                   sx, sy, params.domainWarp.amplitude, params.domainWarp.frequency);
                    }

                    float h = 0.0f;
                    switch (params.fractalType)
                    {
                    case FractalType::None:
                        h = evaluateNoise(perlin.get(), simplex.get(), params.noiseType,
                                         sx * params.frequency, sy * params.frequency);
                        break;
                    case FractalType::FBM:
                        h = fbm(perlin.get(), simplex.get(), params.noiseType,
                                sx, sy, params.octaves, params.frequency,
                                params.lacunarity, params.persistence);
                        break;
                    case FractalType::Ridged:
                        h = ridged(perlin.get(), simplex.get(), params.noiseType,
                                   sx, sy, params.octaves, params.frequency,
                                   params.lacunarity, params.persistence);
                        break;
                    case FractalType::Billowy:
                        h = billowy(perlin.get(), simplex.get(), params.noiseType,
                                    sx, sy, params.octaves, params.frequency,
                                    params.lacunarity, params.persistence);
                        break;
                    }

                    h *= params.amplitude;

                    size_t idx = static_cast<size_t>(y) * outWidth + x;
                    rawHeights[idx] = h;
                    minH = std::min(minH, h);
                    maxH = std::max(maxH, h);
                }

                if (progress)
                    progress(static_cast<float>(y) / static_cast<float>(outHeight) * 0.5f);
            }

            // Second pass: normalize to [0, 255] and write RGBA
            float range = maxH - minH;
            if (range < 1e-6f) range = 1.0f;

            for (uint32_t y = 0; y < outHeight; ++y)
            {
                for (uint32_t x = 0; x < outWidth; ++x)
                {
                    size_t idx = static_cast<size_t>(y) * outWidth + x;
                    float normalized = (rawHeights[idx] - minH) / range;

                    // Post-processing: height power curve
                    if (params.heightExponent != 1.0f)
                        normalized = std::pow(normalized, params.heightExponent);

                    // Post-processing: inversion
                    if (params.invert)
                        normalized = 1.0f - normalized;

                    // Post-processing: terracing
                    if (params.terracing && params.terraceSteps >= 2)
                    {
                        float steps = static_cast<float>(params.terraceSteps);
                        normalized = std::floor(normalized * steps) / (steps - 1.0f);
                        normalized = std::clamp(normalized, 0.0f, 1.0f);
                    }

                    uint8_t val = static_cast<uint8_t>(std::clamp(normalized * 255.0f, 0.0f, 255.0f));

                    size_t pixelIdx = idx * 4;
                    result.rgbaData[pixelIdx + 0] = val; // R
                    result.rgbaData[pixelIdx + 1] = val; // G
                    result.rgbaData[pixelIdx + 2] = val; // B
                    result.rgbaData[pixelIdx + 3] = 255; // A
                }

                if (progress)
                    progress(0.5f + static_cast<float>(y) / static_cast<float>(outHeight) * 0.5f);
            }

            if (progress)
                progress(1.0f);

            return result;
        }
    }

    HeightmapResult HeightmapGenerator::generate(const HeightmapParams& params,
                                                  ProgressCallback progress)
    {
        return generateInternal(params, params.width, params.height, progress);
    }

    HeightmapResult HeightmapGenerator::generatePreview(const HeightmapParams& params)
    {
        return generateInternal(params, 256, 256, nullptr);
    }

    bool HeightmapGenerator::saveAsVFImage(const HeightmapResult& result,
                                            const std::string& outputPath)
    {
        if (!result.valid())
            return false;

        return VFImageWriter::write(outputPath, result.width, result.height, result.rgbaData);
    }
}
