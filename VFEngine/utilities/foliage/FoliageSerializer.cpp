#include "FoliageSerializer.hpp"
#include "../print/Log.hpp"
#include "../resource/VFSHelpers.hpp"
#include "../resource/VirtualFileSystem.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <nlohmann/json.hpp>

namespace foliage
{
    static_assert(sizeof(FoliageInstance) == 56,
                  "FoliageSerializer bulk blob assumes the frozen 56-byte FoliageInstance layout");

    // Guard against a runaway count in a corrupt file (same intent as the billboard reader's cap).
    static constexpr uint32_t FOLIAGE_INSTANCE_COUNT_LIMIT = 5000000;

    bool FoliageSerializer::saveFoliageInstances(const std::string& filePath,
                                                 const std::vector<FoliageInstance>& instances)
    {
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            return false;

        namespace fs = std::filesystem;
        fs::path dir = fs::path(filePath).parent_path();
        if (!dir.empty() && !fs::exists(dir))
            fs::create_directories(dir);

        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        file.write(FOLIAGE_INSTANCE_MAGIC.data(), 4);
        uint32_t version = FOLIAGE_INSTANCE_FORMAT_VERSION;
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));
        uint32_t recordSize = static_cast<uint32_t>(sizeof(FoliageInstance));
        file.write(reinterpret_cast<const char*>(&recordSize), sizeof(recordSize));
        uint32_t count = static_cast<uint32_t>(instances.size());
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));

        if (count > 0)
            file.write(reinterpret_cast<const char*>(instances.data()),
                       static_cast<std::streamsize>(count) * sizeof(FoliageInstance));

        return file.good();
    }

    bool FoliageSerializer::loadFoliageInstances(const std::string& filePath,
                                                 std::vector<FoliageInstance>& instances)
    {
        const auto bytes = resource::readFileBytes(filePath);
        if (bytes.empty()) return false;
        const std::string contents(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream file(contents, std::ios::binary);

        std::array<char, 4> magic{};
        file.read(magic.data(), 4);
        if (magic != FOLIAGE_INSTANCE_MAGIC)
        {
            vfLogError("FoliageSerializer: Invalid magic in {}", filePath);
            return false;
        }

        uint32_t version = 0;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != FOLIAGE_INSTANCE_FORMAT_VERSION)
        {
            vfLogError("FoliageSerializer: Unsupported version {} in {}", version, filePath);
            return false;
        }

        uint32_t recordSize = 0;
        file.read(reinterpret_cast<char*>(&recordSize), sizeof(recordSize));
        if (recordSize != static_cast<uint32_t>(sizeof(FoliageInstance)))
        {
            vfLogError("FoliageSerializer: Record size {} != {} in {}",
                       recordSize, static_cast<uint32_t>(sizeof(FoliageInstance)), filePath);
            return false;
        }

        uint32_t count = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (count > FOLIAGE_INSTANCE_COUNT_LIMIT)
        {
            vfLogError("FoliageSerializer: Instance count {} exceeds limit in {}", count, filePath);
            return false;
        }

        instances.resize(count);
        if (count > 0)
            file.read(reinterpret_cast<char*>(instances.data()),
                      static_cast<std::streamsize>(count) * sizeof(FoliageInstance));

        return file.good();
    }

    bool FoliageSerializer::saveFoliagePalette(const std::string& filePath,
                                               const std::vector<FoliageType>& palette)
    {
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            return false;

        namespace fs = std::filesystem;
        fs::path dir = fs::path(filePath).parent_path();
        if (!dir.empty() && !fs::exists(dir))
            fs::create_directories(dir);

        nlohmann::json j = nlohmann::json::array();
        for (const auto& t : palette)
        {
            nlohmann::json e;
            e["meshPath"] = t.meshPath;
            e["materialPath"] = t.materialPath;
            e["weight"] = t.weight;
            e["densityScale"] = t.densityScale;
            e["affectedByDensityScale"] = t.affectedByDensityScale;
            e["scaleMin"] = t.scaleRange.x;
            e["scaleMax"] = t.scaleRange.y;
            e["heightMin"] = t.heightRange.x;
            e["heightMax"] = t.heightRange.y;
            e["rotYMin"] = t.rotationYRange.x;
            e["rotYMax"] = t.rotationYRange.y;
            e["randomTilt"] = t.randomTilt;
            e["alignToNormal"] = t.alignToNormal;
            e["minSlopeDeg"] = t.minSlopeDeg;
            e["maxSlopeDeg"] = t.maxSlopeDeg;
            e["altitudeMin"] = t.altitudeRange.x;
            e["altitudeMax"] = t.altitudeRange.y;
            e["startCullDistance"] = t.startCullDistance;
            e["endCullDistance"] = t.endCullDistance;
            e["castShadow"] = t.castShadow;
            e["farMode"] = static_cast<int>(t.farMode);
            e["receiveWind"] = t.receiveWind;
            e["windStrength"] = t.windStrength;
            e["windStiffness"] = t.windStiffness;
            e["collision"] = t.collision;
            e["navContribute"] = t.navContribute;
            e["colliderShape"] = static_cast<int>(t.colliderShape);
            e["visible"] = t.visible;
            e["paintEnabled"] = t.paintEnabled;
            j.push_back(e);
        }

        std::ofstream file(filePath);
        if (!file.is_open()) return false;
        file << j.dump(2);
        return file.good();
    }

    bool FoliageSerializer::loadFoliagePalette(const std::string& filePath,
                                               std::vector<FoliageType>& palette)
    {
        try
        {
            nlohmann::json j = resource::readJsonFile(filePath);
            if (j.is_null()) return false;
            palette.clear();
            for (const auto& e : j)
            {
                FoliageType t;
                if (e.contains("meshPath")) t.meshPath = e["meshPath"].get<std::string>();
                if (e.contains("materialPath")) t.materialPath = e["materialPath"].get<std::string>();
                if (e.contains("weight")) t.weight = e["weight"].get<float>();
                if (e.contains("densityScale")) t.densityScale = e["densityScale"].get<float>();
                if (e.contains("affectedByDensityScale")) t.affectedByDensityScale = e["affectedByDensityScale"].get<bool>();
                if (e.contains("scaleMin")) t.scaleRange.x = e["scaleMin"].get<float>();
                if (e.contains("scaleMax")) t.scaleRange.y = e["scaleMax"].get<float>();
                if (e.contains("heightMin")) t.heightRange.x = e["heightMin"].get<float>();
                if (e.contains("heightMax")) t.heightRange.y = e["heightMax"].get<float>();
                if (e.contains("rotYMin")) t.rotationYRange.x = e["rotYMin"].get<float>();
                if (e.contains("rotYMax")) t.rotationYRange.y = e["rotYMax"].get<float>();
                if (e.contains("randomTilt")) t.randomTilt = e["randomTilt"].get<float>();
                if (e.contains("alignToNormal")) t.alignToNormal = e["alignToNormal"].get<bool>();
                if (e.contains("minSlopeDeg")) t.minSlopeDeg = e["minSlopeDeg"].get<float>();
                if (e.contains("maxSlopeDeg")) t.maxSlopeDeg = e["maxSlopeDeg"].get<float>();
                if (e.contains("altitudeMin")) t.altitudeRange.x = e["altitudeMin"].get<float>();
                if (e.contains("altitudeMax")) t.altitudeRange.y = e["altitudeMax"].get<float>();
                if (e.contains("startCullDistance")) t.startCullDistance = e["startCullDistance"].get<float>();
                if (e.contains("endCullDistance")) t.endCullDistance = e["endCullDistance"].get<float>();
                if (e.contains("castShadow")) t.castShadow = e["castShadow"].get<bool>();
                if (e.contains("farMode")) t.farMode = static_cast<FoliageFarMode>(e["farMode"].get<int>());
                if (e.contains("receiveWind")) t.receiveWind = e["receiveWind"].get<bool>();
                if (e.contains("windStrength")) t.windStrength = e["windStrength"].get<float>();
                if (e.contains("windStiffness")) t.windStiffness = e["windStiffness"].get<float>();
                if (e.contains("collision")) t.collision = e["collision"].get<bool>();
                if (e.contains("navContribute")) t.navContribute = e["navContribute"].get<bool>();
                if (e.contains("colliderShape")) t.colliderShape = static_cast<FoliageColliderShape>(e["colliderShape"].get<int>());
                if (e.contains("visible")) t.visible = e["visible"].get<bool>();
                if (e.contains("paintEnabled")) t.paintEnabled = e["paintEnabled"].get<bool>();
                palette.push_back(t);
            }
            return true;
        }
        catch (...)
        {
            vfLogError("FoliageSerializer: Failed to parse palette JSON: {}", filePath);
            return false;
        }
    }
}
