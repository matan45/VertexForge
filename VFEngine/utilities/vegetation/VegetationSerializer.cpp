#include "VegetationSerializer.hpp"
#include "../print/Log.hpp"
#include "../resource/VFSHelpers.hpp"
#include "../resource/VirtualFileSystem.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <random>
#include <nlohmann/json.hpp>

namespace vegetation
{
    bool VegetationSerializer::saveBillboardInstances(const std::string& filePath,
                                                       const std::vector<BillboardInstance>& instances)
    {
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            return false;

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
            file.write(reinterpret_cast<const char*>(&inst.heightScale), sizeof(float));
            file.write(reinterpret_cast<const char*>(&inst.tint), sizeof(float));
            file.write(reinterpret_cast<const char*>(&inst.normal), sizeof(glm::vec3));
            const uint8_t source = static_cast<uint8_t>(inst.source); // v3: Painted/Procedural
            file.write(reinterpret_cast<const char*>(&source), sizeof(uint8_t));
        }

        return file.good();
    }

    bool VegetationSerializer::loadBillboardInstances(const std::string& filePath,
                                                       std::vector<BillboardInstance>& instances)
    {
        const auto bytes = resource::readFileBytes(filePath);
        if (bytes.empty()) return false;
        const std::string contents(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream file(contents, std::ios::binary);

        std::array<char, 4> magic{};
        file.read(magic.data(), 4);
        if (magic != VEGETATION_INSTANCE_MAGIC)
        {
            vfLogError("VegetationSerializer: Invalid magic in {}", filePath);
            return false;
        }

        uint32_t version = 0;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        // v2 and v3 are both readable; v3 appends a per-instance source byte.
        if (version != 2 && version != VEGETATION_INSTANCE_FORMAT_VERSION)
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
            file.read(reinterpret_cast<char*>(&inst.heightScale), sizeof(float));
            file.read(reinterpret_cast<char*>(&inst.tint), sizeof(float));
            file.read(reinterpret_cast<char*>(&inst.normal), sizeof(glm::vec3));
            if (version >= 3)
            {
                uint8_t source = 0;
                file.read(reinterpret_cast<char*>(&source), sizeof(uint8_t));
                inst.source = static_cast<InstanceSource>(source);
            }
            // else v2: inst.source stays the default (Painted) from resize().
            inst.windPhase = windDist(rng);
        }

        return file.good();
    }

    bool VegetationSerializer::saveBillboardPalette(const std::string& filePath,
                                                      const std::vector<BillboardPaletteEntry>& palette)
    {
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            return false;

        namespace fs = std::filesystem;
        fs::path dir = fs::path(filePath).parent_path();
        if (!dir.empty() && !fs::exists(dir))
            fs::create_directories(dir);

        nlohmann::json j = nlohmann::json::array();
        for (const auto& entry : palette)
        {
            nlohmann::json e;
            e["texturePath"] = entry.texturePath;
            e["weight"] = entry.weight;
            e["scaleMin"] = entry.scaleRange.x;
            e["scaleMax"] = entry.scaleRange.y;
            e["heightMin"] = entry.heightRange.x;
            e["heightMax"] = entry.heightRange.y;
            e["tintJitter"] = entry.tintJitter;
            e["mode"] = static_cast<int>(entry.mode);
            e["visible"] = entry.visible;
            e["paintEnabled"] = entry.paintEnabled;
            j.push_back(e);
        }

        std::ofstream file(filePath);
        if (!file.is_open()) return false;
        file << j.dump(2);
        return file.good();
    }

    bool VegetationSerializer::loadBillboardPalette(const std::string& filePath,
                                                      std::vector<BillboardPaletteEntry>& palette)
    {
        try {
            nlohmann::json j = resource::readJsonFile(filePath);
            if (j.is_null()) return false;
            palette.clear();
            for (const auto& e : j)
            {
                BillboardPaletteEntry entry;
                if (e.contains("texturePath")) entry.texturePath = e["texturePath"].get<std::string>();
                if (e.contains("weight")) entry.weight = e["weight"].get<float>();
                if (e.contains("scaleMin")) entry.scaleRange.x = e["scaleMin"].get<float>();
                if (e.contains("scaleMax")) entry.scaleRange.y = e["scaleMax"].get<float>();
                if (e.contains("heightMin")) entry.heightRange.x = e["heightMin"].get<float>();
                if (e.contains("heightMax")) entry.heightRange.y = e["heightMax"].get<float>();
                if (e.contains("tintJitter")) entry.tintJitter = e["tintJitter"].get<float>();
                if (e.contains("mode")) entry.mode = static_cast<BillboardMode>(e["mode"].get<int>());
                if (e.contains("visible")) entry.visible = e["visible"].get<bool>();
                if (e.contains("paintEnabled")) entry.paintEnabled = e["paintEnabled"].get<bool>();
                palette.push_back(entry);
            }
            return true;
        } catch (...) {
            vfLogError("VegetationSerializer: Failed to parse palette JSON: {}", filePath);
            return false;
        }
    }
}
