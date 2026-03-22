#include "VegetationSerializer.hpp"
#include "../print/Log.hpp"
#include <fstream>
#include <filesystem>
#include <random>

namespace vegetation
{
    bool VegetationSerializer::saveBillboardInstances(const std::string& filePath,
                                                       const std::vector<BillboardInstance>& instances)
    {
        namespace fs = std::filesystem;
        fs::path dir = fs::path(filePath).parent_path();
        if (!dir.empty() && !fs::exists(dir))
            fs::create_directories(dir);

        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        file.write(VEGETATION_INSTANCE_MAGIC.data(), 4);
        uint32_t version = VEGETATION_INSTANCE_FORMAT_VERSION;
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));
        uint32_t count = static_cast<uint32_t>(instances.size());
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));

        for (const auto& inst : instances)
        {
            file.write(reinterpret_cast<const char*>(&inst.position), sizeof(glm::vec3));
            file.write(reinterpret_cast<const char*>(&inst.rotation), sizeof(float));
            file.write(reinterpret_cast<const char*>(&inst.scale), sizeof(float));
            file.write(reinterpret_cast<const char*>(&inst.paletteEntryIndex), sizeof(uint32_t));
        }

        return file.good();
    }

    bool VegetationSerializer::loadBillboardInstances(const std::string& filePath,
                                                       std::vector<BillboardInstance>& instances)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        std::array<char, 4> magic{};
        file.read(magic.data(), 4);
        if (magic != VEGETATION_INSTANCE_MAGIC)
        {
            vfLogError("VegetationSerializer: Invalid magic in {}", filePath);
            return false;
        }

        uint32_t version = 0;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != VEGETATION_INSTANCE_FORMAT_VERSION)
        {
            vfLogError("VegetationSerializer: Unsupported version {} in {}", version, filePath);
            return false;
        }

        uint32_t count = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));

        if (count > 1000000)
        {
            vfLogError("VegetationSerializer: Instance count {} exceeds limit in {}", count, filePath);
            return false;
        }

        instances.resize(count);
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> windDist(0.0f, 1.0f);

        for (uint32_t i = 0; i < count; ++i)
        {
            auto& inst = instances[i];
            file.read(reinterpret_cast<char*>(&inst.position), sizeof(glm::vec3));
            file.read(reinterpret_cast<char*>(&inst.rotation), sizeof(float));
            file.read(reinterpret_cast<char*>(&inst.scale), sizeof(float));
            file.read(reinterpret_cast<char*>(&inst.paletteEntryIndex), sizeof(uint32_t));
            inst.windPhase = windDist(rng);
        }

        return file.good();
    }
}
