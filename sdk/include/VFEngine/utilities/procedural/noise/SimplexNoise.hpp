#pragma once
#include <cstdint>
#include <array>

namespace procedural
{
    class SimplexNoise
    {
    public:
        explicit SimplexNoise(uint32_t seed);

        // Evaluate 2D Simplex noise at (x, y), returns value in [-1, 1]
        float evaluate(float x, float y) const;

    private:
        std::array<int, 512> perm{};
        std::array<int, 512> permMod12{};

        static constexpr float F2 = 0.3660254037844386f;  // (sqrt(3) - 1) / 2
        static constexpr float G2 = 0.21132486540518713f; // (3 - sqrt(3)) / 6
    };
}
