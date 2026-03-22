#pragma once

#include "TerrainTypes.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace terrain
{
    struct CaveSDFConfig
    {
        uint32_t resX = 33;
        uint32_t resY = 33;
        uint32_t resZ = 33;
        float voxelSize = 1.0f;      // World units per voxel (XZ, derived from tile config)
        float yVoxelSize = 1.0f;     // Vertical voxel size (matched to XZ for uniform grid)
        float yExtentBelow = 10.0f;  // How far below minHeight the SDF extends

        [[nodiscard]] size_t totalVoxels() const
        {
            return static_cast<size_t>(resX) * resY * resZ;
        }

        static CaveSDFConfig fromTileConfig(const TerrainTileConfig& tileConfig)
        {
            CaveSDFConfig cfg;
            cfg.resX = tileConfig.getVertexCount();
            cfg.resZ = tileConfig.getVertexCount();
            cfg.voxelSize = tileConfig.getVertexSpacing();

            // Match Y voxel size to XZ for uniform grid — no blocky artifacts
            cfg.yVoxelSize = cfg.voxelSize;

            // Compute Y resolution from terrain height range
            float totalYRange = tileConfig.maxHeight - tileConfig.minHeight + cfg.yExtentBelow;
            cfg.resY = std::max(4u, static_cast<uint32_t>(totalYRange / cfg.yVoxelSize) + 1);

            return cfg;
        }
    };

    // SDF convention: negative = solid, positive = air/cave, isosurface at 0
    struct CaveSDFData
    {
        std::vector<float> sdfGrid;
        std::vector<float> originalSdfGrid; // Unmodified heightmap SDF — used to detect carved regions
        CaveSDFConfig config;
        glm::vec3 localOrigin{0.0f}; // World-space bottom corner of SDF volume
        bool isDirty = false;

        CaveSDFData() = default;

        void initialize(const TerrainTileConfig& tileConfig, const glm::vec3& tileWorldOrigin)
        {
            config = CaveSDFConfig::fromTileConfig(tileConfig);

            // SDF volume starts below the tile's min height
            localOrigin = glm::vec3(
                tileWorldOrigin.x,
                tileConfig.minHeight - config.yExtentBelow,
                tileWorldOrigin.z);

            sdfGrid.resize(config.totalVoxels());

            // Initialize all voxels as solid (negative = inside solid)
            std::fill(sdfGrid.begin(), sdfGrid.end(), -1.0f);
        }

        void initializeFromHeightData(const TerrainTileConfig& tileConfig,
                                       const glm::vec3& tileWorldOrigin,
                                       const std::vector<float>& heightData)
        {
            initialize(tileConfig, tileWorldOrigin);

            uint32_t vertexCount = tileConfig.getVertexCount();

            // Set SDF values based on distance to heightmap surface
            // Above surface = positive (air), below surface = negative (solid)
            for (uint32_t z = 0; z < config.resZ; ++z)
            {
                for (uint32_t x = 0; x < config.resX; ++x)
                {
                    // Sample heightmap at this XZ
                    uint32_t hx = std::min(x, vertexCount - 1);
                    uint32_t hz = std::min(z, vertexCount - 1);
                    float surfaceHeight = heightData[hz * vertexCount + hx];

                    for (uint32_t y = 0; y < config.resY; ++y)
                    {
                        float worldY = getWorldY(y);
                        float distToSurface = worldY - surfaceHeight;

                        // Clamp to reasonable range for numerical stability
                        setSDF(x, y, z, std::clamp(distToSurface, -10.0f, 10.0f));
                    }
                }
            }

            // Store original so we can detect which voxels were carved
            originalSdfGrid = sdfGrid;

            isDirty = true;
        }

        [[nodiscard]] bool isInitialized() const { return !sdfGrid.empty(); }

        [[nodiscard]] size_t getIndex(uint32_t x, uint32_t y, uint32_t z) const
        {
            return static_cast<size_t>(z) * config.resX * config.resY +
                   static_cast<size_t>(y) * config.resX +
                   static_cast<size_t>(x);
        }

        [[nodiscard]] float getSDF(uint32_t x, uint32_t y, uint32_t z) const
        {
            if (x >= config.resX || y >= config.resY || z >= config.resZ)
                return -1.0f; // Solid outside bounds
            return sdfGrid[getIndex(x, y, z)];
        }

        void setSDF(uint32_t x, uint32_t y, uint32_t z, float value)
        {
            if (x >= config.resX || y >= config.resY || z >= config.resZ)
                return;
            sdfGrid[getIndex(x, y, z)] = value;
        }

        // Get world-space Y coordinate for a given Y index
        [[nodiscard]] float getWorldY(uint32_t yIndex) const
        {
            return localOrigin.y + static_cast<float>(yIndex) * config.yVoxelSize;
        }

        // Get world-space position for a voxel
        [[nodiscard]] glm::vec3 getWorldPosition(uint32_t x, uint32_t y, uint32_t z) const
        {
            return glm::vec3(
                localOrigin.x + static_cast<float>(x) * config.voxelSize,
                getWorldY(y),
                localOrigin.z + static_cast<float>(z) * config.voxelSize);
        }

        // Trilinear interpolation of SDF at world position
        [[nodiscard]] float sampleSDF(const glm::vec3& worldPos) const
        {
            glm::vec3 local = worldPos - localOrigin;
            float fx = local.x / config.voxelSize;
            float fy = local.y / config.yVoxelSize;
            float fz = local.z / config.voxelSize;

            int x0 = static_cast<int>(std::floor(fx));
            int y0 = static_cast<int>(std::floor(fy));
            int z0 = static_cast<int>(std::floor(fz));

            float tx = fx - static_cast<float>(x0);
            float ty = fy - static_cast<float>(y0);
            float tz = fz - static_cast<float>(z0);

            auto sample = [this](int x, int y, int z) -> float
            {
                uint32_t cx = static_cast<uint32_t>(std::clamp(x, 0, static_cast<int>(config.resX) - 1));
                uint32_t cy = static_cast<uint32_t>(std::clamp(y, 0, static_cast<int>(config.resY) - 1));
                uint32_t cz = static_cast<uint32_t>(std::clamp(z, 0, static_cast<int>(config.resZ) - 1));
                return getSDF(cx, cy, cz);
            };

            // Trilinear interpolation
            float c000 = sample(x0, y0, z0);
            float c100 = sample(x0 + 1, y0, z0);
            float c010 = sample(x0, y0 + 1, z0);
            float c110 = sample(x0 + 1, y0 + 1, z0);
            float c001 = sample(x0, y0, z0 + 1);
            float c101 = sample(x0 + 1, y0, z0 + 1);
            float c011 = sample(x0, y0 + 1, z0 + 1);
            float c111 = sample(x0 + 1, y0 + 1, z0 + 1);

            float c00 = c000 * (1.0f - tx) + c100 * tx;
            float c10 = c010 * (1.0f - tx) + c110 * tx;
            float c01 = c001 * (1.0f - tx) + c101 * tx;
            float c11 = c011 * (1.0f - tx) + c111 * tx;

            float c0 = c00 * (1.0f - ty) + c10 * ty;
            float c1 = c01 * (1.0f - ty) + c11 * ty;

            return c0 * (1.0f - tz) + c1 * tz;
        }

        // Compute SDF gradient (approximate normal) at voxel position
        [[nodiscard]] glm::vec3 computeGradient(uint32_t x, uint32_t y, uint32_t z) const
        {
            float dx = getSDF(std::min(x + 1, config.resX - 1), y, z) -
                        getSDF(x > 0 ? x - 1 : 0, y, z);
            float dy = getSDF(x, std::min(y + 1, config.resY - 1), z) -
                        getSDF(x, y > 0 ? y - 1 : 0, z);
            float dz = getSDF(x, y, std::min(z + 1, config.resZ - 1)) -
                        getSDF(x, y, z > 0 ? z - 1 : 0);

            glm::vec3 grad(dx, dy, dz);
            float len = glm::length(grad);
            return len > 1e-6f ? grad / len : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        // Check if any voxel was carved (differs from original heightmap SDF)
        [[nodiscard]] bool hasCaveGeometry() const
        {
            if (originalSdfGrid.empty())
            {
                // No original = all-solid init, any positive means cave
                for (float v : sdfGrid)
                {
                    if (v > 0.0f)
                        return true;
                }
                return false;
            }

            for (size_t i = 0; i < sdfGrid.size(); ++i)
            {
                if (std::abs(sdfGrid[i] - originalSdfGrid[i]) > 1e-4f)
                    return true;
            }
            return false;
        }

        // Check if a specific cube was modified by carving AND touches the original solid volume.
        // Skips cubes entirely above the original terrain surface (all original corners positive).
        [[nodiscard]] bool isCubeModified(uint32_t x, uint32_t y, uint32_t z) const
        {
            if (originalSdfGrid.empty())
                return true; // No original data, process all

            bool anyModified = false;
            bool anyOriginalSolid = false;

            // Check all 8 corners of the cube
            for (int dz = 0; dz <= 1; ++dz)
            {
                for (int dy = 0; dy <= 1; ++dy)
                {
                    for (int dx = 0; dx <= 1; ++dx)
                    {
                        uint32_t cx = x + dx;
                        uint32_t cy = y + dy;
                        uint32_t cz = z + dz;
                        if (cx >= config.resX || cy >= config.resY || cz >= config.resZ)
                            continue;
                        size_t idx = getIndex(cx, cy, cz);

                        if (originalSdfGrid[idx] <= 0.0f)
                            anyOriginalSolid = true;

                        if (std::abs(sdfGrid[idx] - originalSdfGrid[idx]) > 1e-4f)
                            anyModified = true;
                    }
                }
            }

            // Only generate mesh in cubes that were originally part of the terrain volume
            return anyModified && anyOriginalSolid;
        }

        void clear()
        {
            sdfGrid.clear();
            originalSdfGrid.clear();
            config = CaveSDFConfig{};
            localOrigin = glm::vec3(0.0f);
            isDirty = false;
        }
    };

} // namespace terrain
