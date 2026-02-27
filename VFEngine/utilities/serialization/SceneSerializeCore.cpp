#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"

// Helper to clean null terminators from strings
static void cleanNullTerminators(std::string& str)
{
    if (auto pos = str.find('\0'); pos != std::string::npos)
        str.resize(pos);
}

namespace serialization
{
    json SceneSerialization::serializeTransform(const components::TransformComponent& transform)
    {
        json j;
        j["position"] = json::array({transform.position.x, transform.position.y, transform.position.z});
        j["rotation"] = json::array({transform.rotation.x, transform.rotation.y, transform.rotation.z});
        j["scale"] = json::array({transform.scale.x, transform.scale.y, transform.scale.z});
        return j;
    }

    json SceneSerialization::serializeCamera(const components::CameraComponent& camera)
    {
        json j;
        j["cameraId"] = camera.cameraId;
        j["fieldOfView"] = camera.fieldOfView;
        j["nearPlane"] = camera.nearPlane;
        j["farPlane"] = camera.farPlane;
        j["aspectRatio"] = camera.aspectRatio;
        j["isPerspective"] = camera.isPerspective;
        j["isPrimary"] = camera.isPrimary;
        j["showFrustum"] = camera.showFrustum;
        j["orthoSize"] = camera.orthoSize;
        return j;
    }

    json SceneSerialization::serializeIBL(const components::IBLComponent& ibl)
    {
        json j;
        std::string cleanFileName = ibl.fileName;
        cleanNullTerminators(cleanFileName);
        j["fileName"] = cleanFileName;
        return j;
    }

    json SceneSerialization::serializeMesh(const components::MeshComponent& mesh)
    {
        json j;
        std::string cleanPath = mesh.meshPath;
        cleanNullTerminators(cleanPath);
        j["meshPath"] = cleanPath;
        j["showBoundingBox"] = mesh.showBoundingBox;

        if (!mesh.animatorPath.empty())
        {
            std::string cleanAnimatorPath = mesh.animatorPath;
            cleanNullTerminators(cleanAnimatorPath);
            j["animatorPath"] = cleanAnimatorPath;

            if (mesh.applyRootMotion)
            {
                j["applyRootMotion"] = true;
            }
        }

        if (mesh.maxDrawDistance > 0.0f)
        {
            j["maxDrawDistance"] = mesh.maxDrawDistance;
        }
        return j;
    }

    void SceneSerialization::deserializeTransform(const json& j, components::TransformComponent& transform)
    {
        if (auto it = j.find("position"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            transform.position = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("rotation"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            transform.rotation = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("scale"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            transform.scale = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        transform.isDirty = true;
    }

    void SceneSerialization::deserializeCamera(const json& j, components::CameraComponent& camera)
    {
        if (auto it = j.find("cameraId"); it != j.end() && it->is_number_unsigned())
            camera.cameraId = it->get<uint32_t>();
        if (auto it = j.find("fieldOfView"); it != j.end() && it->is_number())
            camera.fieldOfView = it->get<float>();
        if (auto it = j.find("nearPlane"); it != j.end() && it->is_number())
            camera.nearPlane = it->get<float>();
        if (auto it = j.find("farPlane"); it != j.end() && it->is_number())
            camera.farPlane = it->get<float>();
        if (auto it = j.find("aspectRatio"); it != j.end() && it->is_number())
            camera.aspectRatio = it->get<float>();
        if (auto it = j.find("isPerspective"); it != j.end() && it->is_boolean())
            camera.isPerspective = it->get<bool>();
        if (auto it = j.find("isPrimary"); it != j.end() && it->is_boolean())
            camera.isPrimary = it->get<bool>();
        if (auto it = j.find("showFrustum"); it != j.end() && it->is_boolean())
            camera.showFrustum = it->get<bool>();
        if (auto it = j.find("orthoSize"); it != j.end() && it->is_number())
            camera.orthoSize = it->get<float>();
        camera.updateProjectionMatrix();
    }

    std::string SceneSerialization::deserializeIBL(const json& j)
    {
        if (auto it = j.find("fileName"); it != j.end() && it->is_string())
        {
            return it->get<std::string>();
        }
        return "";
    }

    void SceneSerialization::deserializeMesh(const json& j, components::MeshComponent& mesh)
    {
        if (auto it = j.find("meshPath"); it != j.end() && it->is_string())
        {
            mesh.meshPath = it->get<std::string>();
        }
        if (auto it = j.find("showBoundingBox"); it != j.end() && it->is_boolean())
        {
            mesh.showBoundingBox = it->get<bool>();
        }
        if (auto it = j.find("animatorPath"); it != j.end() && it->is_string())
        {
            mesh.animatorPath = it->get<std::string>();
        }
        if (auto it = j.find("applyRootMotion"); it != j.end() && it->is_boolean())
        {
            mesh.applyRootMotion = it->get<bool>();
        }
        if (auto it = j.find("maxDrawDistance"); it != j.end() && it->is_number())
        {
            mesh.maxDrawDistance = it->get<float>();
        }
    }

    json SceneSerialization::serializeMaterial(const components::MaterialComponent& material)
    {
        json j;

        std::string cleanDefaultPath = material.defaultMaterial;
        cleanNullTerminators(cleanDefaultPath);
        j["defaultMaterial"] = cleanDefaultPath;

        json subMeshMaterialsJson = json::object();
        for (const auto& [submeshName, matPath] : material.subMeshMaterials)
        {
            std::string cleanMatPath = matPath;
            cleanNullTerminators(cleanMatPath);
            subMeshMaterialsJson[submeshName] = cleanMatPath;
        }
        j["subMeshMaterials"] = subMeshMaterialsJson;

        json paramOverridesJson = json::object();
        for (const auto& [paramName, value] : material.parameterOverrides)
        {
            paramOverridesJson[paramName] = value;
        }
        j["parameterOverrides"] = paramOverridesJson;

        return j;
    }

    void SceneSerialization::deserializeMaterial(const json& j, components::MaterialComponent& material)
    {
        if (auto it = j.find("defaultMaterial"); it != j.end() && it->is_string())
        {
            material.defaultMaterial = it->get<std::string>();
        }

        if (auto it = j.find("subMeshMaterials"); it != j.end() && it->is_object())
        {
            material.subMeshMaterials.clear();
            for (auto& [key, value] : it->items())
            {
                if (value.is_string())
                {
                    material.subMeshMaterials[key] = value.get<std::string>();
                }
            }
        }

        if (auto it = j.find("parameterOverrides"); it != j.end() && it->is_object())
        {
            material.parameterOverrides.clear();
            for (auto& [key, value] : it->items())
            {
                if (value.is_number())
                {
                    material.parameterOverrides[key] = value.get<float>();
                }
            }
        }
    }

    std::string SceneSerialization::billboardSizeModeToString(components::BillboardSizeMode mode)
    {
        switch (mode)
        {
        case components::BillboardSizeMode::WorldSpace: return "worldSpace";
        default: return "screenSpace";
        }
    }

    components::BillboardSizeMode SceneSerialization::stringToBillboardSizeMode(const std::string& str)
    {
        if (str == "worldSpace") return components::BillboardSizeMode::WorldSpace;
        return components::BillboardSizeMode::ScreenSpace;
    }

    std::string SceneSerialization::billboardIconTypeToString(components::BillboardIconType type)
    {
        switch (type)
        {
        case components::BillboardIconType::DirectionalLight: return "directionalLight";
        case components::BillboardIconType::PointLight: return "pointLight";
        case components::BillboardIconType::SpotLight: return "spotLight";
        case components::BillboardIconType::Camera: return "camera";
        case components::BillboardIconType::Audio2D: return "audio2D";
        case components::BillboardIconType::Audio3D: return "audio3D";
        case components::BillboardIconType::Particle: return "particle";
        case components::BillboardIconType::Billboard: return "billboard";
        case components::BillboardIconType::Text: return "text";
        default: return "custom";
        }
    }

    components::BillboardIconType SceneSerialization::stringToBillboardIconType(const std::string& str)
    {
        if (str == "directionalLight") return components::BillboardIconType::DirectionalLight;
        if (str == "pointLight") return components::BillboardIconType::PointLight;
        if (str == "spotLight") return components::BillboardIconType::SpotLight;
        if (str == "camera") return components::BillboardIconType::Camera;
        if (str == "audio2D") return components::BillboardIconType::Audio2D;
        if (str == "audio3D") return components::BillboardIconType::Audio3D;
        if (str == "particle") return components::BillboardIconType::Particle;
        if (str == "billboard") return components::BillboardIconType::Billboard;
        if (str == "text") return components::BillboardIconType::Text;
        // Legacy support
        if (str == "light") return components::BillboardIconType::PointLight;
        if (str == "audioSource") return components::BillboardIconType::Audio3D;
        return components::BillboardIconType::Custom;
    }

    json SceneSerialization::serializeBillboard(const components::BillboardComponent& billboard)
    {
        json j;
        j["iconType"] = billboardIconTypeToString(billboard.iconType);
        j["atlasIndex"] = billboard.atlasIndex;
        j["sizeMode"] = billboardSizeModeToString(billboard.sizeMode);
        j["size"] = json::array({billboard.size.x, billboard.size.y});
        j["colorTint"] = json::array({
            billboard.colorTint.r, billboard.colorTint.g, billboard.colorTint.b, billboard.colorTint.a
        });
        j["editorOnly"] = billboard.editorOnly;
        j["selectable"] = billboard.selectable;
        if (!billboard.texturePath.empty())
        {
            j["texturePath"] = billboard.texturePath;
        }
        if (!billboard.renderTextureSourceName.empty())
        {
            j["renderTextureSourceName"] = billboard.renderTextureSourceName;
        }
        return j;
    }

    void SceneSerialization::deserializeBillboard(const json& j, components::BillboardComponent& billboard)
    {
        billboard.iconType = stringToBillboardIconType(j.value("iconType", "custom"));
        billboard.atlasIndex = j.value("atlasIndex", 0u);
        billboard.sizeMode = stringToBillboardSizeMode(j.value("sizeMode", "screenSpace"));
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2)
        {
            billboard.size = glm::vec2(j["size"][0].get<float>(), j["size"][1].get<float>());
        }
        if (j.contains("colorTint") && j["colorTint"].is_array() && j["colorTint"].size() >= 4)
        {
            billboard.colorTint = glm::vec4(
                j["colorTint"][0].get<float>(), j["colorTint"][1].get<float>(),
                j["colorTint"][2].get<float>(), j["colorTint"][3].get<float>()
            );
        }
        billboard.editorOnly = j.value("editorOnly", true);
        billboard.selectable = j.value("selectable", true);
        billboard.texturePath = j.value("texturePath", std::string(""));
        billboard.renderTextureSourceName = j.value("renderTextureSourceName", std::string(""));
        billboard.renderTextureSource = entt::null; // Resolved post-load
    }

    json SceneSerialization::serializeText(const components::TextComponent& text)
    {
        json j;
        j["fontPath"] = text.fontPath;
        j["text"] = text.text;
        j["fontSize"] = text.fontSize;
        j["color"] = json::array({text.color.r, text.color.g, text.color.b, text.color.a});
        j["lineSpacing"] = text.lineSpacing;
        j["letterSpacing"] = text.letterSpacing;
        j["maxWidth"] = text.maxWidth;
        return j;
    }

    void SceneSerialization::deserializeText(const json& j, components::TextComponent& text)
    {
        text.fontPath = j.value("fontPath", std::string(""));
        text.text = j.value("text", std::string("Hello World"));
        text.fontSize = j.value("fontSize", 32.0f);
        if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 4)
        {
            text.color = glm::vec4(
                j["color"][0].get<float>(), j["color"][1].get<float>(),
                j["color"][2].get<float>(), j["color"][3].get<float>()
            );
        }
        text.lineSpacing = j.value("lineSpacing", 1.0f);
        text.letterSpacing = j.value("letterSpacing", 0.0f);
        text.maxWidth = j.value("maxWidth", 0.0f);
    }
}
