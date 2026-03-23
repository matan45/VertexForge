#include "PerlinNoise.hpp"
#include <numeric>
#include <algorithm>
#include <random>
#include <cmath>

namespace procedural
{
    // 12 unit-length gradient vectors for 2D Perlin noise
    // Diagonals normalized to 1/sqrt(2) ≈ 0.7071 so all gradients have magnitude 1.0
    namespace
    {
        constexpr float INV_SQRT2 = 0.70710678f;
        constexpr float gradients[][2] = {
            { 1.0f,      0.0f     }, {-1.0f,      0.0f     },
            { 0.0f,      1.0f     }, { 0.0f,     -1.0f     },
            { INV_SQRT2, INV_SQRT2}, {-INV_SQRT2, INV_SQRT2},
            { INV_SQRT2,-INV_SQRT2}, {-INV_SQRT2,-INV_SQRT2},
            { 1.0f,      0.0f     }, {-1.0f,      0.0f     },
            { 0.0f,      1.0f     }, { 0.0f,     -1.0f     }
        };
    }

    PerlinNoise::PerlinNoise(uint32_t seed)
    {
        std::array<int, 256> p;
        std::iota(p.begin(), p.end(), 0);

        std::mt19937 rng(seed);
        std::shuffle(p.begin(), p.end(), rng);

        for (int i = 0; i < 256; ++i)
        {
            perm[i] = p[i];
            perm[i + 256] = p[i];
        }
    }

    float PerlinNoise::fade(float t)
    {
        // Improved Perlin quintic interpolation: 6t^5 - 15t^4 + 10t^3
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    float PerlinNoise::lerp(float t, float a, float b)
    {
        return a + t * (b - a);
    }

    float PerlinNoise::grad(int hash, float x, float y)
    {
        // Select one of 12 gradient vectors using hash
        int idx = hash % 12;
        return gradients[idx][0] * x + gradients[idx][1] * y;
    }

    float PerlinNoise::evaluate(float x, float y) const
    {
        // Grid cell coordinates
        int xi = static_cast<int>(std::floor(x)) & 255;
        int yi = static_cast<int>(std::floor(y)) & 255;

        // Fractional position within cell
        float xf = x - std::floor(x);
        float yf = y - std::floor(y);

        // Fade curves for interpolation
        float u = fade(xf);
        float v = fade(yf);

        // Hash coordinates of the 4 corners
        int aa = perm[perm[xi    ] + yi    ];
        int ab = perm[perm[xi    ] + yi + 1];
        int ba = perm[perm[xi + 1] + yi    ];
        int bb = perm[perm[xi + 1] + yi + 1];

        // Gradient dot products at each corner
        float g00 = grad(aa, xf,        yf);
        float g10 = grad(ba, xf - 1.0f, yf);
        float g01 = grad(ab, xf,        yf - 1.0f);
        float g11 = grad(bb, xf - 1.0f, yf - 1.0f);

        // Bilinear interpolation
        float x1 = lerp(u, g00, g10);
        float x2 = lerp(u, g01, g11);
        return lerp(v, x1, x2);
    }
}
