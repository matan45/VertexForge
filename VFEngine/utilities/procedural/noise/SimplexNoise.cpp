#include "SimplexNoise.hpp"
#include <numeric>
#include <algorithm>
#include <random>
#include <cmath>

namespace procedural
{
    namespace
    {
        // 12 gradient vectors uniformly distributed on the unit circle
        // This avoids directional bias from axis-aligned or diagonal-only sets
        constexpr float grad2[][2] = {
            { 1.0f,       0.0f      },  //   0 deg
            { 0.866025f,  0.5f      },  //  30 deg
            { 0.5f,       0.866025f },  //  60 deg
            { 0.0f,       1.0f      },  //  90 deg
            {-0.5f,       0.866025f },  // 120 deg
            {-0.866025f,  0.5f      },  // 150 deg
            {-1.0f,       0.0f      },  // 180 deg
            {-0.866025f, -0.5f      },  // 210 deg
            {-0.5f,      -0.866025f },  // 240 deg
            { 0.0f,      -1.0f      },  // 270 deg
            { 0.5f,      -0.866025f },  // 300 deg
            { 0.866025f, -0.5f      }   // 330 deg
        };

        float dot2(const float g[2], float x, float y)
        {
            return g[0] * x + g[1] * y;
        }
    }

    SimplexNoise::SimplexNoise(uint32_t seed)
    {
        std::array<int, 256> p;
        std::iota(p.begin(), p.end(), 0);

        std::mt19937 rng(seed);
        std::shuffle(p.begin(), p.end(), rng);

        for (int i = 0; i < 256; ++i)
        {
            perm[i] = p[i];
            perm[i + 256] = p[i];
            permMod12[i] = perm[i] % 12;
            permMod12[i + 256] = perm[i] % 12;
        }
    }

    float SimplexNoise::evaluate(float x, float y) const
    {
        // Skew input space to determine which simplex cell we're in
        float s = (x + y) * F2;
        int i = static_cast<int>(std::floor(x + s));
        int j = static_cast<int>(std::floor(y + s));

        float t = static_cast<float>(i + j) * G2;
        float X0 = static_cast<float>(i) - t;
        float Y0 = static_cast<float>(j) - t;
        float x0 = x - X0;
        float y0 = y - Y0;

        // Determine which simplex triangle we're in
        int i1, j1;
        if (x0 > y0) { i1 = 1; j1 = 0; }
        else          { i1 = 0; j1 = 1; }

        float x1 = x0 - static_cast<float>(i1) + G2;
        float y1 = y0 - static_cast<float>(j1) + G2;
        float x2 = x0 - 1.0f + 2.0f * G2;
        float y2 = y0 - 1.0f + 2.0f * G2;

        int ii = i & 255;
        int jj = j & 255;

        // Calculate contributions from the three corners
        float n0 = 0.0f, n1 = 0.0f, n2 = 0.0f;

        float t0 = 0.5f - x0 * x0 - y0 * y0;
        if (t0 >= 0.0f)
        {
            t0 *= t0;
            int gi0 = permMod12[ii + perm[jj]];
            n0 = t0 * t0 * dot2(grad2[gi0], x0, y0);
        }

        float t1 = 0.5f - x1 * x1 - y1 * y1;
        if (t1 >= 0.0f)
        {
            t1 *= t1;
            int gi1 = permMod12[ii + i1 + perm[jj + j1]];
            n1 = t1 * t1 * dot2(grad2[gi1], x1, y1);
        }

        float t2 = 0.5f - x2 * x2 - y2 * y2;
        if (t2 >= 0.0f)
        {
            t2 *= t2;
            int gi2 = permMod12[ii + 1 + perm[jj + 1]];
            n2 = t2 * t2 * dot2(grad2[gi2], x2, y2);
        }

        // Scale to approximately [-1, 1]
        // With unit-length gradients, the max contribution is ~0.0225 per corner,
        // so 3 corners * 0.0225 ≈ 0.0675; scaling by 45.23 gives [-1, 1] range
        return 45.23f * (n0 + n1 + n2);
    }
}
