#include "PerlinNoise.hpp"
#include <numeric>
#include <algorithm>
#include <random>
#include <cmath>

namespace procedural
{
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
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    float PerlinNoise::lerp(float t, float a, float b)
    {
        return a + t * (b - a);
    }

    float PerlinNoise::grad(int hash, float x, float y)
    {
        int h = hash & 7;
        float u = h < 4 ? x : y;
        float v = h < 4 ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
    }

    float PerlinNoise::evaluate(float x, float y) const
    {
        int xi = static_cast<int>(std::floor(x)) & 255;
        int yi = static_cast<int>(std::floor(y)) & 255;

        float xf = x - std::floor(x);
        float yf = y - std::floor(y);

        float u = fade(xf);
        float v = fade(yf);

        int aa = perm[perm[xi] + yi];
        int ab = perm[perm[xi] + yi + 1];
        int ba = perm[perm[xi + 1] + yi];
        int bb = perm[perm[xi + 1] + yi + 1];

        float x1 = lerp(u, grad(aa, xf, yf), grad(ba, xf - 1.0f, yf));
        float x2 = lerp(u, grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f));

        return lerp(v, x1, x2);
    }
}
