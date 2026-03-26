#pragma once
#include "SparseVoxelOctree.hpp"
#include <string>

namespace volumetric
{
    class VolumetricSerializer
    {
    public:
        static bool save(const std::string& filePath, const SparseVoxelOctree& grid);
        static bool load(const std::string& filePath, SparseVoxelOctree& outGrid);

    private:
        static constexpr uint32_t VOLUMETRIC_FILE_MAGIC = 0x564E564F;   // "VNVO"
        static constexpr uint32_t VOLUMETRIC_FILE_VERSION = 1;

        struct FileHeader
        {
            uint32_t magic = VOLUMETRIC_FILE_MAGIC;
            uint32_t version = VOLUMETRIC_FILE_VERSION;
            float originX = 0.0f;
            float originY = 0.0f;
            float originZ = 0.0f;
            float voxelSize = 1.0f;
            int32_t dimX = 0;
            int32_t dimY = 0;
            int32_t dimZ = 0;
            uint32_t dataSize = 0;
        };
    };
}
