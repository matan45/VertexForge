#pragma once

#include <cmath>
#include <cstdint>

namespace terrain
{
    // Hash-based 2D value noise in [0,1] for scatter/clumping masks. This is the single
    // source of truth shared by the vegetation brush and the mesh brush (VK-1578); keep it
    // dependency-free (<cmath> only) so it stays trivially portable and unit-testable.
    inline float valueNoise2D(float x, float z, uint32_t seed)
    {
        auto hash = [seed](int ix, int iz) -> float {
            uint32_t h = static_cast<uint32_t>(ix) * 374761393u
                       + static_cast<uint32_t>(iz) * 668265263u + seed * 362437u;
            h = (h ^ (h >> 13)) * 1274126177u;
            h ^= (h >> 16);
            return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
        };
        int x0 = static_cast<int>(std::floor(x));
        int z0 = static_cast<int>(std::floor(z));
        float fx = x - static_cast<float>(x0);
        float fz = z - static_cast<float>(z0);
        float ux = fx * fx * (3.0f - 2.0f * fx);
        float uz = fz * fz * (3.0f - 2.0f * fz);
        float n00 = hash(x0, z0),     n10 = hash(x0 + 1, z0);
        float n01 = hash(x0, z0 + 1), n11 = hash(x0 + 1, z0 + 1);
        float nx0 = n00 + (n10 - n00) * ux;
        float nx1 = n01 + (n11 - n01) * ux;
        return nx0 + (nx1 - nx0) * uz;
    }
}
