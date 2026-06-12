#include "MaterialInstanceAsset.hpp"
#include "../print/Log.hpp"
#include "../uuid/UUID.hpp"
#include "../asset/AssetRef.hpp"
#include "../resource/VFSHelpers.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <format>

namespace material
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    std::string MaterialInstanceAsset::textureSlotToString(TextureSlot slot)
    {
        switch (slot)
        {
        case TextureSlot::Albedo: return "Albedo";
        case TextureSlot::Normal: return "Normal";
        case TextureSlot::ORM: return "ORM";
        case TextureSlot::Metallic: return "Metallic";
        case TextureSlot::Roughness: return "Roughness";
        case TextureSlot::AO: return "AO";
        case TextureSlot::Emission: return "Emission";
        case TextureSlot::Height: return "Height";
        default: return "Custom" + std::to_string(static_cast<int>(slot));
        }
    }

    TextureSlot MaterialInstanceAsset::stringToTextureSlot(const std::string& str)
    {
        if (str == "Albedo") return TextureSlot::Albedo;
        if (str == "Normal") return TextureSlot::Normal;
        if (str == "ORM") return TextureSlot::ORM;
        if (str == "Metallic") return TextureSlot::Metallic;
        if (str == "Roughness") return TextureSlot::Roughness;
        if (str == "AO") return TextureSlot::AO;
        if (str == "Emission") return TextureSlot::Emission;
        if (str == "Height") return TextureSlot::Height;
        // Handle custom slots (Custom8, Custom9, etc.)
        if (str.starts_with("Custom"))
        {
            try
            {
                int index = std::stoi(str.substr(6));
                if (index >= 0 && index < static_cast<int>(TextureSlot::Count))
                {
                    return static_cast<TextureSlot>(index);
                }
            }
            catch (...)
            {
            }
        }
        return TextureSlot::Albedo; // Default fallback
    }

    json MaterialInstanceAsset::serializeTextureOverrides(const std::map<TextureSlot, asset::AssetRef>& overrides)
    {
        json j = json::object();
        for (const auto& [slot, ref] : overrides)
        {
            if (ref.isValid())
            {
                std::string slotName = textureSlotToString(slot);
                j[slotName] = ref.toHexString();
                const std::string& texPath = ref.resolve();
                if (!texPath.empty())
                {
                    j[slotName + "Path"] = texPath;
                }
            }
        }
        return j;
    }

    std::map<TextureSlot, asset::AssetRef> MaterialInstanceAsset::deserializeTextureOverrides(const json& j)
    {
        std::map<TextureSlot, asset::AssetRef> overrides;
        if (!j.is_object()) return overrides;

        for (auto& [key, value] : j.items())
        {
            // Skip path fallback entries
            if (key.ends_with("Path")) continue;

            if (value.is_string())
            {
                TextureSlot slot = stringToTextureSlot(key);
                std::string hexStr = value.get<std::string>();
                if (!hexStr.empty())
                {
                    auto ref = asset::AssetRef::fromHexString(hexStr);
                    // If GUID is valid but can't resolve, try the stored path fallback
                    if (ref.isValid() && ref.resolve().empty())
                    {
                        std::string pathKey = key + "Path";
                        if (auto pathIt = j.find(pathKey); pathIt != j.end() && pathIt->is_string())
                        {
                            std::string fallbackPath = pathIt->get<std::string>();
                            if (!fallbackPath.empty())
                            {
                                auto pathRef = asset::AssetRef::fromPath(fallbackPath);
                                if (pathRef.isValid())
                                {
                                    ref = pathRef;
                                }
                            }
                        }
                    }
                    if (ref.isValid())
                    {
                        overrides[slot] = ref;
                    }
                }
            }
        }
        return overrides;
    }

    std::optional<MaterialInstanceData> MaterialInstanceAsset::load(std::string_view path)
    {
        fs::path filePath(path);

        // Validate file exists
        if (!fs::exists(filePath))
        {
            vfLogError("Material instance file not found: {}", path);
            return std::nullopt;
        }

        // Check file size (sanity check)
        std::error_code ec;
        auto fileSize = fs::file_size(filePath, ec);
        if (ec)
        {
            vfLogError("Cannot read material instance file size '{}': {}", path, ec.message());
            return std::nullopt;
        }
        constexpr size_t MAX_FILE_SIZE = 1 * 1024 * 1024; // 1 MB limit (instances are small)
        if (fileSize > MAX_FILE_SIZE)
        {
            vfLogError("Material instance file '{}' is too large ({} bytes)", path, fileSize);
            return std::nullopt;
        }

        // Parse JSON
        json j;
        try
        {
            j = resource::readJsonFile(filePath.string());
        }
        catch (const json::parse_error& e)
        {
            vfLogError("Material instance file '{}' contains invalid JSON: {}", path, e.what());
            return std::nullopt;
        }
        if (j.is_null())
        {
            vfLogError("Failed to open material instance file: {}", path);
            return std::nullopt;
        }

        if (!j.is_object())
        {
            vfLogError("Material instance file '{}' must contain a JSON object", path);
            return std::nullopt;
        }

        MaterialInstanceData instance;

        try
        {
            // Check type field
            std::string type = j.value("type", "");
            if (type != "MaterialInstance")
            {
                vfLogWarning("Material instance file '{}' has unexpected type '{}'", path, type);
            }

            // Check version (1.0 is forward-compatible — 1.1 only adds override maps)
            std::string fileVersion = j.value("version", MATERIAL_INSTANCE_FORMAT_VERSION);
            if (fileVersion != MATERIAL_INSTANCE_FORMAT_VERSION && fileVersion != "1.0")
            {
                vfLogWarning("Material instance '{}' has version {} (current is {})",
                             path, fileVersion, MATERIAL_INSTANCE_FORMAT_VERSION);
            }

            // Basic properties
            instance.uuid = j.value("uuid", std::to_string(uuid::UUID().getValue()));
            instance.name = j.value("name", "Unnamed Instance");
            std::string parentRefStr = j.value("parentMaterialRef", "");
            if (parentRefStr.empty())
            {
                vfLogError("Material instance '{}' has no parent material specified", path);
                return std::nullopt;
            }
            instance.parentMaterialRef = asset::AssetRef::fromHexString(parentRefStr);

            // If GUID is valid but can't resolve, try the stored path fallback
            if (instance.parentMaterialRef.isValid() && instance.parentMaterialRef.resolve().empty())
            {
                std::string fallbackPath = j.value("parentMaterialRefPath", "");
                if (!fallbackPath.empty())
                {
                    auto pathRef = asset::AssetRef::fromPath(fallbackPath);
                    if (pathRef.isValid())
                    {
                        instance.parentMaterialRef = pathRef;
                    }
                }
            }

            // Validate parent ref
            if (!instance.parentMaterialRef.isValid())
            {
                vfLogError("Material instance '{}' has invalid parent material ref", path);
                return std::nullopt;
            }

            // Validate parent is not an instance (no nested instances)
            std::string parentResolvedPath = instance.parentMaterialRef.resolve();
            if (isInstanceFile(parentResolvedPath))
            {
                vfLogError("Material instance '{}' cannot have another instance as parent", path);
                return std::nullopt;
            }

            // Texture overrides
            if (j.contains("textureOverrides"))
            {
                instance.textureOverrides = deserializeTextureOverrides(j["textureOverrides"]);
            }

            // Named parameter overrides (format 1.1). Tolerant: unknown names are kept —
            // they resolve to nothing until the parent re-exposes the parameter.
            if (j.contains("parameterOverrides") && j["parameterOverrides"].is_object())
            {
                for (auto& [name, entry] : j["parameterOverrides"].items())
                {
                    if (!entry.is_object() || !entry.contains("value")) continue;
                    const auto& value = entry["value"];
                    std::string type = entry.value("type", "scalar");

                    if (type == "scalar" && value.is_number())
                    {
                        instance.parameterOverrides[name] = value.get<float>();
                    }
                    else if (type == "vec2" && value.is_array() && value.size() >= 2)
                    {
                        instance.parameterOverrides[name] =
                            glm::vec2(value[0].get<float>(), value[1].get<float>());
                    }
                    else if (type == "vec3" && value.is_array() && value.size() >= 3)
                    {
                        instance.parameterOverrides[name] =
                            glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
                    }
                    else if (type == "vec4" && value.is_array() && value.size() >= 4)
                    {
                        instance.parameterOverrides[name] = glm::vec4(
                            value[0].get<float>(), value[1].get<float>(),
                            value[2].get<float>(), value[3].get<float>());
                    }
                    else
                    {
                        vfLogWarning("Material instance '{}': parameter override '{}' has malformed value, skipping",
                                     path, name);
                    }
                }
            }

            // Named texture parameter overrides (format 1.1)
            if (j.contains("textureParameterOverrides") && j["textureParameterOverrides"].is_object())
            {
                for (auto& [name, entry] : j["textureParameterOverrides"].items())
                {
                    if (!entry.is_object()) continue;
                    std::string hexStr = entry.value("guid", "");
                    if (hexStr.empty()) continue;

                    auto ref = asset::AssetRef::fromHexString(hexStr);
                    if (ref.isValid() && ref.resolve().empty())
                    {
                        std::string fallbackPath = entry.value("path", "");
                        if (!fallbackPath.empty())
                        {
                            auto pathRef = asset::AssetRef::fromPath(fallbackPath);
                            if (pathRef.isValid())
                            {
                                ref = pathRef;
                            }
                        }
                    }
                    if (ref.isValid())
                    {
                        instance.textureParameterOverrides[name] = ref;
                    }
                }
            }

            // Scalar overrides
            if (j.contains("scalarOverrides"))
            {
                const auto& scalars = j["scalarOverrides"];
                if (scalars.is_object())
                {
                    if (scalars.contains("albedo") && scalars["albedo"].is_array() && scalars["albedo"].size() >= 4)
                    {
                        instance.albedoOverride = glm::vec4(
                            scalars["albedo"][0].get<float>(),
                            scalars["albedo"][1].get<float>(),
                            scalars["albedo"][2].get<float>(),
                            scalars["albedo"][3].get<float>());
                    }
                    if (scalars.contains("metallic") && scalars["metallic"].is_number())
                    {
                        instance.metallicOverride = scalars["metallic"].get<float>();
                    }
                    if (scalars.contains("roughness") && scalars["roughness"].is_number())
                    {
                        instance.roughnessOverride = scalars["roughness"].get<float>();
                    }
                    if (scalars.contains("ao") && scalars["ao"].is_number())
                    {
                        instance.aoOverride = scalars["ao"].get<float>();
                    }
                    if (scalars.contains("emission") && scalars["emission"].is_number())
                    {
                        instance.emissionOverride = scalars["emission"].get<float>();
                    }
                    if (scalars.contains("iblDiffuse") && scalars["iblDiffuse"].is_number())
                    {
                        instance.iblDiffuseOverride = scalars["iblDiffuse"].get<float>();
                    }
                    if (scalars.contains("iblSpecular") && scalars["iblSpecular"].is_number())
                    {
                        instance.iblSpecularOverride = scalars["iblSpecular"].get<float>();
                    }
                }
            }

            return instance;
        }
        catch (const json::exception& e)
        {
            vfLogError("Failed to parse material instance '{}': {}", path, e.what());
            return std::nullopt;
        }
        catch (const std::exception& e)
        {
            vfLogError("Unexpected error loading material instance '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    bool MaterialInstanceAsset::save(std::string_view path, const MaterialInstanceData& instance)
    {
        json j;

        j["version"] = MATERIAL_INSTANCE_FORMAT_VERSION;
        j["type"] = "MaterialInstance";
        j["uuid"] = instance.uuid;
        j["name"] = instance.name;
        j["parentMaterialRef"] = instance.parentMaterialRef.toHexString();
        {
            const std::string& parentPath = instance.parentMaterialRef.resolve();
            if (!parentPath.empty())
            {
                j["parentMaterialRefPath"] = parentPath;
            }
        }

        // Texture overrides
        if (!instance.textureOverrides.empty())
        {
            j["textureOverrides"] = serializeTextureOverrides(instance.textureOverrides);
        }

        // Named parameter overrides (format 1.1)
        if (!instance.parameterOverrides.empty())
        {
            json params = json::object();
            for (const auto& [name, value] : instance.parameterOverrides)
            {
                json entry;
                std::visit([&entry](const auto& v)
                {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, float>)
                    {
                        entry["type"] = "scalar";
                        entry["value"] = v;
                    }
                    else if constexpr (std::is_same_v<T, glm::vec2>)
                    {
                        entry["type"] = "vec2";
                        entry["value"] = json::array({v.x, v.y});
                    }
                    else if constexpr (std::is_same_v<T, glm::vec3>)
                    {
                        entry["type"] = "vec3";
                        entry["value"] = json::array({v.x, v.y, v.z});
                    }
                    else if constexpr (std::is_same_v<T, glm::vec4>)
                    {
                        entry["type"] = "vec4";
                        entry["value"] = json::array({v.x, v.y, v.z, v.w});
                    }
                }, value);
                params[name] = entry;
            }
            j["parameterOverrides"] = params;
        }

        // Named texture parameter overrides (format 1.1)
        if (!instance.textureParameterOverrides.empty())
        {
            json params = json::object();
            for (const auto& [name, ref] : instance.textureParameterOverrides)
            {
                if (!ref.isValid()) continue;
                json entry;
                entry["guid"] = ref.toHexString();
                const std::string& texPath = ref.resolve();
                if (!texPath.empty())
                {
                    entry["path"] = texPath;
                }
                params[name] = entry;
            }
            if (!params.empty())
            {
                j["textureParameterOverrides"] = params;
            }
        }

        // Scalar overrides
        json scalars = json::object();
        if (instance.albedoOverride.has_value())
        {
            const auto& a = *instance.albedoOverride;
            scalars["albedo"] = json::array({a.x, a.y, a.z, a.w});
        }
        if (instance.metallicOverride.has_value())
        {
            scalars["metallic"] = *instance.metallicOverride;
        }
        if (instance.roughnessOverride.has_value())
        {
            scalars["roughness"] = *instance.roughnessOverride;
        }
        if (instance.aoOverride.has_value())
        {
            scalars["ao"] = *instance.aoOverride;
        }
        if (instance.emissionOverride.has_value())
        {
            scalars["emission"] = *instance.emissionOverride;
        }
        if (instance.iblDiffuseOverride.has_value())
        {
            scalars["iblDiffuse"] = *instance.iblDiffuseOverride;
        }
        if (instance.iblSpecularOverride.has_value())
        {
            scalars["iblSpecular"] = *instance.iblSpecularOverride;
        }
        if (!scalars.empty())
        {
            j["scalarOverrides"] = scalars;
        }

        // Write to file
        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create material instance file: {}", path);
                return false;
            }

            file << j.dump(4); // Pretty print with 4-space indent

            // Flush to OS buffers before closing to avoid race conditions
            // where readers might see incomplete/stale data
            file.flush();
            if (!file.good())
            {
                vfLogError("Failed to flush material instance file: {}", path);
                return false;
            }

            file.close();
            if (file.fail())
            {
                vfLogError("Failed to close material instance file: {}", path);
                return false;
            }

            vfLogInfo("Saved material instance: {} to {}", instance.name, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save material instance '{}': {}", path, e.what());
            return false;
        }
    }

    MaterialInstanceData MaterialInstanceAsset::createDefault(const std::string& name, const asset::AssetRef& parentRef)
    {
        MaterialInstanceData instance;
        instance.uuid = std::to_string(uuid::UUID().getValue());
        instance.name = name;
        instance.parentMaterialRef = parentRef;
        // No overrides by default - inherit everything from parent
        return instance;
    }
}
