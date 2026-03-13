#include "VegetationSerializer.hpp"
#include "../print/Log.hpp"
#include "../resource/EndianUtils.hpp"
#include <fstream>
#include <filesystem>

namespace vegetation
{
    namespace fs = std::filesystem;
    using namespace resource::endian;

    static bool ensureParentDirectory(const std::string& filePath)
    {
        fs::path path(filePath);
        if (path.has_parent_path())
        {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            if (ec)
            {
                vfLogError("VegetationSerializer: Failed to create directory {}: {}",
                           path.parent_path().string(), ec.message());
                return false;
            }
        }
        return true;
    }

    bool VegetationSerializer::saveDensityMap(const std::string& filePath,
                                              const VegetationDensityMap& densityMap)
    {
        if (!densityMap.isInitialized())
        {
            vfLogError("VegetationSerializer: Cannot save uninitialized density map");
            return false;
        }

        if (!ensureParentDirectory(filePath)) return false;

        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("VegetationSerializer: Failed to open file for writing: {}", filePath);
            return false;
        }

        // Magic number
        file.write(VEGETATION_DENSITY_MAGIC.data(), 4);

        // Version
        writeLE(file, VEGETATION_FORMAT_VERSION);

        // Resolution
        writeLE(file, densityMap.resolution);

        // Density data: resolution * resolution floats
        writeVectorLE(file, densityMap.densityData);

        if (!file.good())
        {
            vfLogError("VegetationSerializer: Write error saving density map to {}", filePath);
            return false;
        }

        return true;
    }

    bool VegetationSerializer::loadDensityMap(const std::string& filePath,
                                              VegetationDensityMap& densityMap)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("VegetationSerializer: Failed to open file for reading: {}", filePath);
            return false;
        }

        // Validate magic number
        std::array<char, 4> magic{};
        file.read(magic.data(), 4);
        if (magic != VEGETATION_DENSITY_MAGIC)
        {
            vfLogError("VegetationSerializer: Invalid magic bytes in density map file: {}", filePath);
            return false;
        }

        // Version check
        uint32_t version = readLE<uint32_t>(file);
        if (version != VEGETATION_FORMAT_VERSION)
        {
            vfLogError("VegetationSerializer: Incompatible density map version {}, expected {}. Re-import required.",
                       version, VEGETATION_FORMAT_VERSION);
            return false;
        }

        // Resolution
        uint32_t resolution = readLE<uint32_t>(file);
        if (resolution == 0 || resolution > MAX_DENSITY_RESOLUTION)
        {
            vfLogError("VegetationSerializer: Invalid density map resolution {} in file: {}",
                       resolution, filePath);
            return false;
        }

        // Density data
        size_t texelCount = static_cast<size_t>(resolution) * resolution;
        readVectorLE(file, densityMap.densityData, texelCount);

        if (!file.good())
        {
            vfLogError("VegetationSerializer: Read error loading density map from {}", filePath);
            densityMap.clear();
            return false;
        }

        densityMap.resolution = resolution;
        return true;
    }
}
