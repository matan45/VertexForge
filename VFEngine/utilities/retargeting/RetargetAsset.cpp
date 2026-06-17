#include "RetargetAsset.hpp"
#include "HumanoidBoneAutoMap.hpp"
#include "../resource/Types.hpp"
#include "../resource/VFSHelpers.hpp"
#include "../print/Log.hpp"

#include <nlohmann/json.hpp>
#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace retargeting
{
    namespace
    {
        json quatToJson(const glm::quat& q) { return json::array({q.w, q.x, q.y, q.z}); }

        glm::quat jsonToQuat(const json& j)
        {
            if (j.is_array() && j.size() == 4)
                return glm::quat(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        bool writeJson(const json& j, std::string_view path, const char* what)
        {
            try
            {
                fs::path filePath(path);
                if (filePath.has_parent_path())
                    fs::create_directories(filePath.parent_path());

                std::ofstream file(filePath);
                if (!file.is_open())
                {
                    vfLogError("Failed to create {} file: {}", what, path);
                    return false;
                }
                file << j.dump(4);
                file.flush();
                if (!file.good())
                {
                    vfLogError("Failed to write {} file: {}", what, path);
                    return false;
                }
            }
            catch (const std::exception& e)
            {
                vfLogError("Exception writing {} '{}': {}", what, path, e.what());
                return false;
            }
            return true;
        }
    }

    // ---------------------------------------------------------------- HumanoidRigAsset

    std::optional<HumanoidRigData> HumanoidRigAsset::load(std::string_view path)
    {
        if (!fs::exists(fs::path(path)))
        {
            vfLogError("Humanoid rig file not found: {}", path);
            return std::nullopt;
        }

        json j;
        try
        {
            j = resource::readJsonFile(std::string(path));
        }
        catch (const json::parse_error& e)
        {
            vfLogError("Humanoid rig '{}' contains invalid JSON: {}", path, e.what());
            return std::nullopt;
        }
        if (!j.is_object())
        {
            vfLogError("Humanoid rig '{}' must contain a JSON object", path);
            return std::nullopt;
        }

        try
        {
            const std::string type = j.value("type", "");
            if (type != "HumanoidRig")
                vfLogWarning("Humanoid rig '{}' has unexpected type '{}'", path, type);

            HumanoidRigData rig;
            rig.name = j.value("name", "");
            rig.sourceSkeletonAssetGuid = j.value("sourceSkeletonAssetGuid", "");
            rig.referenceHeight = j.value("referenceHeight", 1.0f);

            if (j.contains("bindings") && j["bindings"].is_array())
            {
                for (const auto& b : j["bindings"])
                {
                    HumanoidBoneBinding binding;
                    binding.role = humanoidBoneRoleFromName(b.value("role", "None"));
                    if (binding.role == HumanoidBoneRole::None) continue;
                    binding.boneName = b.value("bone", "");
                    binding.retargetTranslation = b.value("retargetTranslation", false);
                    if (b.contains("refRot"))
                        binding.referenceLocalRotation = jsonToQuat(b["refRot"]);
                    rig.bindings.push_back(std::move(binding));
                }
            }
            return rig;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to parse humanoid rig '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    bool HumanoidRigAsset::save(std::string_view path, const HumanoidRigData& rig)
    {
        json j;
        j["formatVersion"] = RIG_FORMAT_VERSION;
        j["type"] = "HumanoidRig";
        j["name"] = rig.name;
        j["sourceSkeletonAssetGuid"] = rig.sourceSkeletonAssetGuid;
        j["referenceHeight"] = rig.referenceHeight;

        json bindings = json::array();
        for (const auto& b : rig.bindings)
        {
            json e;
            e["role"] = humanoidBoneRoleName(b.role);
            e["bone"] = b.boneName;
            e["retargetTranslation"] = b.retargetTranslation;
            e["refRot"] = quatToJson(b.referenceLocalRotation);
            bindings.push_back(std::move(e));
        }
        j["bindings"] = std::move(bindings);

        return writeJson(j, path, "humanoid rig");
    }

    HumanoidRigData HumanoidRigAsset::createFromSkeleton(const resource::SkeletonData& skeleton,
                                                         const std::string& sourceSkeletonGuidHex)
    {
        HumanoidRigData rig;
        rig.name = skeleton.name;
        rig.sourceSkeletonAssetGuid = sourceSkeletonGuidHex;
        rig.bindings = autoMapHumanoidBones(skeleton);

        // Reference height = vertical extent of the model-space bind pose (proxy for
        // overall character height; used to normalize hip translation between rigs).
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        for (const auto& m : skeleton.bindPoses)
        {
            const float y = m[3].y;
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
        rig.referenceHeight = (maxY > minY) ? (maxY - minY) : 1.0f;
        return rig;
    }

    // ---------------------------------------------------------------- RetargetMapAsset

    std::optional<RetargetMapData> RetargetMapAsset::load(std::string_view path)
    {
        if (!fs::exists(fs::path(path)))
        {
            vfLogError("Retarget map file not found: {}", path);
            return std::nullopt;
        }

        json j;
        try
        {
            j = resource::readJsonFile(std::string(path));
        }
        catch (const json::parse_error& e)
        {
            vfLogError("Retarget map '{}' contains invalid JSON: {}", path, e.what());
            return std::nullopt;
        }
        if (!j.is_object())
        {
            vfLogError("Retarget map '{}' must contain a JSON object", path);
            return std::nullopt;
        }

        try
        {
            const std::string type = j.value("type", "");
            if (type != "RetargetMap")
                vfLogWarning("Retarget map '{}' has unexpected type '{}'", path, type);

            RetargetMapData map;
            map.name = j.value("name", "");
            map.sourceRigAssetGuid = j.value("sourceRig", "");
            map.targetRigAssetGuid = j.value("targetRig", "");
            map.bakeRootMotion = j.value("bakeRootMotion", false);

            if (j.contains("overrides") && j["overrides"].is_array())
            {
                for (const auto& o : j["overrides"])
                {
                    RetargetRoleOverride ov;
                    ov.role = humanoidBoneRoleFromName(o.value("role", "None"));
                    if (ov.role == HumanoidBoneRole::None) continue;
                    ov.enabled = o.value("enabled", true);
                    ov.overrideTranslation = o.value("overrideTranslation", false);
                    map.overrides.push_back(ov);
                }
            }
            return map;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to parse retarget map '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    bool RetargetMapAsset::save(std::string_view path, const RetargetMapData& map)
    {
        json j;
        j["formatVersion"] = RETARGET_FORMAT_VERSION;
        j["type"] = "RetargetMap";
        j["name"] = map.name;
        j["sourceRig"] = map.sourceRigAssetGuid;
        j["targetRig"] = map.targetRigAssetGuid;
        j["bakeRootMotion"] = map.bakeRootMotion;

        json overrides = json::array();
        for (const auto& o : map.overrides)
        {
            json e;
            e["role"] = humanoidBoneRoleName(o.role);
            e["enabled"] = o.enabled;
            e["overrideTranslation"] = o.overrideTranslation;
            overrides.push_back(std::move(e));
        }
        j["overrides"] = std::move(overrides);

        return writeJson(j, path, "retarget map");
    }
}
