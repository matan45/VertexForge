#pragma once
#include <cstdint>
#include <array>

namespace procedural
{
    class PerlinNoise
    {
    public:
        explicit PerlinNoise(uint32_t seed);

        // Evaluate 2D Perlin noise at (x, y), returns value in [-1, 1]
        float evaluate(float x, float y) const;

    private:
        std::array<int, 512> perm{};

        static float fade(float t);
        static float lerp(float t, float a, float b);
        static float grad(int hash, float x, float y);
    };
}
