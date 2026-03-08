#include "VegetationDensityMap.hpp"
#include <algorithm>
#include <cmath>

namespace vegetation
{
    float VegetationDensityMap::getDensity(uint32_t x, uint32_t z) const
    {
        if (x >= resolution || z >= resolution) return 0.0f;
        return densityData[z * resolution + x];
    }

    void VegetationDensityMap::setDensity(uint32_t x, uint32_t z, float value)
    {
        if (x >= resolution || z >= resolution) return;
        densityData[z * resolution + x] = std::clamp(value, 0.0f, 1.0f);
    }

    float VegetationDensityMap::sampleBilinear(float u, float v) const
    {
        if (!isInitialized()) return 0.0f;

        float fx = u * static_cast<float>(resolution - 1);
        float fz = v * static_cast<float>(resolution - 1);

        uint32_t x0 = static_cast<uint32_t>(std::floor(fx));
        uint32_t z0 = static_cast<uint32_t>(std::floor(fz));
        uint32_t x1 = std::min(x0 + 1, resolution - 1);
        uint32_t z1 = std::min(z0 + 1, resolution - 1);

        float tx = fx - static_cast<float>(x0);
        float tz = fz - static_cast<float>(z0);

        float d00 = getDensity(x0, z0);
        float d10 = getDensity(x1, z0);
        float d01 = getDensity(x0, z1);
        float d11 = getDensity(x1, z1);

        float top = d00 * (1.0f - tx) + d10 * tx;
        float bottom = d01 * (1.0f - tx) + d11 * tx;
        return top * (1.0f - tz) + bottom * tz;
    }

    void VegetationDensityMap::initializeDefault(uint32_t vertexResolution)
    {
        resolution = vertexResolution;
        densityData.assign(static_cast<size_t>(resolution) * resolution, 0.0f);
    }

    void VegetationDensityMap::clear()
    {
        densityData.clear();
        resolution = 0;
    }
}
