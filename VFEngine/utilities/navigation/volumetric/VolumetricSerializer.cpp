#include "VolumetricSerializer.hpp"
#include "../../print/Log.hpp"
#include <fstream>

namespace volumetric
{
    bool VolumetricSerializer::save(const std::string& filePath, const SparseVoxelOctree& grid)
    {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("VolumetricSerializer: Failed to open file for writing: {}", filePath);
            return false;
        }

        FileHeader header;
        header.originX = grid.getOrigin().x;
        header.originY = grid.getOrigin().y;
        header.originZ = grid.getOrigin().z;
        header.voxelSize = grid.getVoxelSize();
        header.dimX = grid.getDims().x;
        header.dimY = grid.getDims().y;
        header.dimZ = grid.getDims().z;
        header.dataSize = static_cast<uint32_t>(grid.getGrid().size());

        file.write(reinterpret_cast<const char*>(&header), sizeof(FileHeader));

        if (header.dataSize > 0)
        {
            file.write(reinterpret_cast<const char*>(grid.getGrid().data()), header.dataSize);
        }

        return file.good();
    }

    bool VolumetricSerializer::load(const std::string& filePath, SparseVoxelOctree& outGrid)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("VolumetricSerializer: Failed to open file for reading: {}", filePath);
            return false;
        }

        FileHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(FileHeader));

        if (header.magic != VOLUMETRIC_FILE_MAGIC)
        {
            vfLogError("VolumetricSerializer: Invalid file magic in: {}", filePath);
            return false;
        }

        if (header.version != VOLUMETRIC_FILE_VERSION)
        {
            vfLogError("VolumetricSerializer: Unsupported version {} in: {}", header.version, filePath);
            return false;
        }

        glm::vec3 origin(header.originX, header.originY, header.originZ);
        glm::ivec3 dims(header.dimX, header.dimY, header.dimZ);

        std::vector<uint8_t> data;
        if (header.dataSize > 0)
        {
            data.resize(header.dataSize);
            file.read(reinterpret_cast<char*>(data.data()), header.dataSize);
        }

        if (!file.good())
        {
            vfLogError("VolumetricSerializer: Read error in: {}", filePath);
            return false;
        }

        outGrid.build(data, dims, origin, header.voxelSize);
        return true;
    }
}
