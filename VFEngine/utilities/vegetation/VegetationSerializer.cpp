#include "VegetationSerializer.hpp"
#include "../print/Log.hpp"
#include "../resource/EndianUtils.hpp"
#include <fstream>
#include <filesystem>

namespace vegetation
{
    namespace fs = std::filesystem;
    using namespace resource::endian;

    bool VegetationSerializer::saveDensityMap(const std::string& filePath,
                                              const VegetationDensityMap& densityMap)
    {
        if (!densityMap.isInitialized())
        {
            vfLogError("VegetationSerializer: Cannot save uninitialized density map");
            return false;
        }

        // Ensure parent directory exists
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

    bool VegetationSerializer::savePlacementData(const std::string& filePath,
                                                  const VegetationPlacementData& placement)
    {
        // Ensure parent directory exists
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

        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("VegetationSerializer: Failed to open file for writing: {}", filePath);
            return false;
        }

        // Magic number
        file.write(VEGETATION_PLACEMENT_MAGIC.data(), 4);

        // Version
        writeLE(file, VEGETATION_FORMAT_VERSION);

        // Instance count
        uint32_t instanceCount = static_cast<uint32_t>(placement.getInstanceCount());
        writeLE(file, instanceCount);

        // Per instance: position (3 floats), rotation (1 float), scale (1 float), speciesId (uint32_t)
        for (const auto& instance : placement.instances)
        {
            writeLE(file, instance.position.x);
            writeLE(file, instance.position.y);
            writeLE(file, instance.position.z);
            writeLE(file, instance.rotation);
            writeLE(file, instance.scale);
            writeLE(file, instance.speciesId);
        }

        if (!file.good())
        {
            vfLogError("VegetationSerializer: Write error saving placement data to {}", filePath);
            return false;
        }

        return true;
    }

    bool VegetationSerializer::loadPlacementData(const std::string& filePath,
                                                  VegetationPlacementData& placement)
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
        if (magic != VEGETATION_PLACEMENT_MAGIC)
        {
            vfLogError("VegetationSerializer: Invalid magic bytes in placement file: {}", filePath);
            return false;
        }

        // Version check
        uint32_t version = readLE<uint32_t>(file);
        if (version != VEGETATION_FORMAT_VERSION)
        {
            vfLogError("VegetationSerializer: Incompatible placement version {}, expected {}. Re-import required.",
                       version, VEGETATION_FORMAT_VERSION);
            return false;
        }

        // Instance count
        uint32_t instanceCount = readLE<uint32_t>(file);
        if (instanceCount > MAX_PLACEMENT_INSTANCES)
        {
            vfLogError("VegetationSerializer: Unreasonable instance count {} in file: {}",
                       instanceCount, filePath);
            return false;
        }

        // Read instances
        placement.instances.clear();
        placement.instances.reserve(instanceCount);

        for (uint32_t i = 0; i < instanceCount; ++i)
        {
            VegetationInstance instance;
            instance.position.x = readLE<float>(file);
            instance.position.y = readLE<float>(file);
            instance.position.z = readLE<float>(file);
            instance.rotation = readLE<float>(file);
            instance.scale = readLE<float>(file);
            instance.speciesId = readLE<uint32_t>(file);
            placement.instances.push_back(instance);
        }

        if (!file.good())
        {
            vfLogError("VegetationSerializer: Read error loading placement data from {}", filePath);
            placement.clear();
            return false;
        }

        return true;
    }
}
