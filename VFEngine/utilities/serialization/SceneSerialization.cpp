#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"
#include <fstream>
#include <algorithm>

namespace serialization
{
    // Helper to clean null terminators from strings
    static void cleanNullTerminators(std::string& str)
    {
        if (auto pos = str.find('\0'); pos != std::string::npos)
            str.resize(pos);
    }

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
        }
        return j;
    }

    json SceneSerialization::serializeEntity(scene::Entity& entity)
    {
        json entityJson;

        entityJson["uuid"] = entity.getUUID().getValue();
        entityJson["name"] = entity.getName();

        if (entity.hasComponent<components::NameComponent>())
        {
            entityJson["isActive"] = entity.getComponent<components::NameComponent>().isActive;
        }

        if (entity.hasComponent<components::TransformComponent>())
        {
            entityJson["transform"] = serializeTransform(entity.getComponent<components::TransformComponent>());
        }

        json componentsJson = json::object();

        if (entity.hasComponent<components::CameraComponent>())
        {
            componentsJson["camera"] = serializeCamera(entity.getComponent<components::CameraComponent>());
        }

        if (entity.hasComponent<components::IBLComponent>())
        {
            componentsJson["ibl"] = serializeIBL(entity.getComponent<components::IBLComponent>());
        }

        if (entity.hasComponent<components::MeshComponent>())
        {
            componentsJson["mesh"] = serializeMesh(entity.getComponent<components::MeshComponent>());
        }

        if (entity.hasComponent<components::MaterialComponent>())
        {
            componentsJson["material"] = serializeMaterial(entity.getComponent<components::MaterialComponent>());
        }

        if (entity.hasComponent<components::BillboardComponent>())
        {
            componentsJson["billboard"] = serializeBillboard(entity.getComponent<components::BillboardComponent>());
        }

        if (entity.hasComponent<components::TextComponent>())
        {
            componentsJson["text"] = serializeText(entity.getComponent<components::TextComponent>());
        }

        if (entity.hasComponent<components::AudioSource2DComponent>())
        {
            componentsJson["audioSource2D"] = serializeAudioSource2D(
                entity.getComponent<components::AudioSource2DComponent>());
        }

        if (entity.hasComponent<components::AudioSource3DComponent>())
        {
            componentsJson["audioSource3D"] = serializeAudioSource3D(
                entity.getComponent<components::AudioSource3DComponent>());
        }

        if (entity.hasComponent<components::ScriptComponent>())
        {
            componentsJson["script"] = serializeScript(entity.getComponent<components::ScriptComponent>());
        }

        if (entity.hasComponent<components::ColliderComponent>())
        {
            componentsJson["collider"] = serializeCollider(entity.getComponent<components::ColliderComponent>());
        }

        if (entity.hasComponent<components::RigidBodyComponent>())
        {
            componentsJson["rigidBody"] = serializeRigidBody(entity.getComponent<components::RigidBodyComponent>());
        }

        if (entity.hasComponent<components::VFXComponent>())
        {
            componentsJson["vfx"] = serializeVFX(entity.getComponent<components::VFXComponent>());
        }

        if (entity.hasComponent<components::DirectionalLightComponent>())
        {
            componentsJson["directionalLight"] = serializeDirectionalLight(
                entity.getComponent<components::DirectionalLightComponent>());
        }

        if (entity.hasComponent<components::PointLightComponent>())
        {
            componentsJson["pointLight"] = serializePointLight(
                entity.getComponent<components::PointLightComponent>());
        }

        if (entity.hasComponent<components::SpotLightComponent>())
        {
            componentsJson["spotLight"] = serializeSpotLight(
                entity.getComponent<components::SpotLightComponent>());
        }

        if (entity.hasComponent<components::TerrainComponent>())
        {
            componentsJson["terrain"] = serializeTerrain(
                entity.getComponent<components::TerrainComponent>());
        }

        if (entity.hasComponent<components::TerrainTileComponent>())
        {
            componentsJson["terrainTile"] = serializeTerrainTile(
                entity.getComponent<components::TerrainTileComponent>());
        }

        if (entity.hasComponent<components::UICanvasComponent>())
        {
            componentsJson["uiCanvas"] = serializeUICanvas(
                entity.getComponent<components::UICanvasComponent>());
        }

        if (entity.hasComponent<components::UIRectComponent>())
        {
            componentsJson["uiRect"] = serializeUIRect(
                entity.getComponent<components::UIRectComponent>());
        }

        if (entity.hasComponent<components::UIImageComponent>())
        {
            componentsJson["uiImage"] = serializeUIImage(
                entity.getComponent<components::UIImageComponent>());
        }

        if (entity.hasComponent<components::UIScrollComponent>())
        {
            componentsJson["uiScroll"] = serializeUIScroll(
                entity.getComponent<components::UIScrollComponent>());
        }

        if (entity.hasComponent<components::UILayoutGroupComponent>())
        {
            componentsJson["uiLayoutGroup"] = serializeUILayoutGroup(
                entity.getComponent<components::UILayoutGroupComponent>());
        }

        if (entity.hasComponent<components::UILabelComponent>())
        {
            componentsJson["uiLabel"] = serializeUILabel(
                entity.getComponent<components::UILabelComponent>());
        }

        if (entity.hasComponent<components::UIButtonComponent>())
        {
            componentsJson["uiButton"] = serializeUIButton(
                entity.getComponent<components::UIButtonComponent>());
        }

        if (entity.hasComponent<components::UITextInputComponent>())
        {
            componentsJson["uiTextInput"] = serializeUITextInput(
                entity.getComponent<components::UITextInputComponent>());
        }

        if (entity.hasComponent<components::UICheckboxComponent>())
        {
            componentsJson["uiCheckbox"] = serializeUICheckbox(
                entity.getComponent<components::UICheckboxComponent>());
        }

        if (entity.hasComponent<components::UIDropdownComponent>())
        {
            componentsJson["uiDropdown"] = serializeUIDropdown(
                entity.getComponent<components::UIDropdownComponent>());
        }

        if (entity.hasComponent<components::UITabsComponent>())
        {
            componentsJson["uiTabs"] = serializeUITabs(
                entity.getComponent<components::UITabsComponent>());
        }

        if (entity.hasComponent<components::UISliderComponent>())
        {
            componentsJson["uiSlider"] = serializeUISlider(
                entity.getComponent<components::UISliderComponent>());
        }

        entityJson["components"] = componentsJson;

        json childrenJson = json::array();
        for (auto& child : entity.getChildren())
        {
            childrenJson.push_back(serializeEntity(child));
        }
        entityJson["children"] = childrenJson;

        return entityJson;
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

    json SceneSerialization::serializeAudioSource2D(const components::AudioSource2DComponent& audioSource)
    {
        json j;
        std::string cleanPath = audioSource.audioFilePath;
        cleanNullTerminators(cleanPath);
        j["audioFilePath"] = cleanPath;
        j["volume"] = audioSource.volume;
        j["pitch"] = audioSource.pitch;
        j["loop"] = audioSource.loop;
        return j;
    }

    void SceneSerialization::deserializeAudioSource2D(const json& j, components::AudioSource2DComponent& audioSource)
    {
        if (auto it = j.find("audioFilePath"); it != j.end() && it->is_string())
        {
            audioSource.audioFilePath = it->get<std::string>();
        }
        if (auto it = j.find("volume"); it != j.end() && it->is_number())
        {
            audioSource.volume = it->get<float>();
        }
        if (auto it = j.find("pitch"); it != j.end() && it->is_number())
        {
            audioSource.pitch = it->get<float>();
        }
        if (auto it = j.find("loop"); it != j.end() && it->is_boolean())
        {
            audioSource.loop = it->get<bool>();
        }
        // Reset runtime state
        audioSource.activeHandle = 0;
        audioSource.isPlaying = false;
    }

    json SceneSerialization::serializeAudioSource3D(const components::AudioSource3DComponent& audioSource)
    {
        json j;
        std::string cleanPath = audioSource.audioFilePath;
        cleanNullTerminators(cleanPath);
        j["audioFilePath"] = cleanPath;
        j["volume"] = audioSource.volume;
        j["pitch"] = audioSource.pitch;
        j["loop"] = audioSource.loop;
        j["minDistance"] = audioSource.minDistance;
        j["maxDistance"] = audioSource.maxDistance;
        j["showDebugSpheres"] = audioSource.showDebugSpheres;
        return j;
    }

    void SceneSerialization::deserializeAudioSource3D(const json& j, components::AudioSource3DComponent& audioSource)
    {
        if (auto it = j.find("audioFilePath"); it != j.end() && it->is_string())
        {
            audioSource.audioFilePath = it->get<std::string>();
        }
        if (auto it = j.find("volume"); it != j.end() && it->is_number())
        {
            audioSource.volume = it->get<float>();
        }
        if (auto it = j.find("pitch"); it != j.end() && it->is_number())
        {
            audioSource.pitch = it->get<float>();
        }
        if (auto it = j.find("loop"); it != j.end() && it->is_boolean())
        {
            audioSource.loop = it->get<bool>();
        }
        if (auto it = j.find("minDistance"); it != j.end() && it->is_number())
        {
            audioSource.minDistance = it->get<float>();
        }
        if (auto it = j.find("maxDistance"); it != j.end() && it->is_number())
        {
            audioSource.maxDistance = it->get<float>();
        }
        if (auto it = j.find("showDebugSpheres"); it != j.end() && it->is_boolean())
        {
            audioSource.showDebugSpheres = it->get<bool>();
        }
        // Reset runtime state
        audioSource.activeHandle = 0;
        audioSource.isPlaying = false;
    }

    json SceneSerialization::serializeScript(const components::ScriptComponent& script)
    {
        json j;
        json scriptsArray = json::array();
        for (const auto& entry : script.scripts)
        {
            json entryJson;
            std::string cleanPath = entry.scriptPath;
            cleanNullTerminators(cleanPath);
            entryJson["scriptPath"] = cleanPath;
            entryJson["enabled"] = entry.enabled;
            scriptsArray.push_back(entryJson);
        }
        j["scripts"] = scriptsArray;
        return j;
    }

    void SceneSerialization::deserializeScript(const json& j, components::ScriptComponent& script)
    {
        script.scripts.clear();
        if (j.contains("scripts") && j["scripts"].is_array())
        {
            for (const auto& entryJson : j["scripts"])
            {
                components::ScriptEntry entry;
                if (entryJson.contains("scriptPath") && entryJson["scriptPath"].is_string())
                {
                    entry.scriptPath = entryJson["scriptPath"].get<std::string>();
                }
                if (entryJson.contains("enabled") && entryJson["enabled"].is_boolean())
                {
                    entry.enabled = entryJson["enabled"].get<bool>();
                }
                // Reset runtime state
                entry.started = false;
                entry.instanceId = 0;
                script.scripts.push_back(entry);
            }
        }
    }

    std::string SceneSerialization::rigidBodyTypeToString(components::RigidBodyType type)
    {
        switch (type)
        {
        case components::RigidBodyType::Static: return "static";
        case components::RigidBodyType::Kinematic: return "kinematic";
        default: return "dynamic";
        }
    }

    components::RigidBodyType SceneSerialization::stringToRigidBodyType(const std::string& str)
    {
        if (str == "static") return components::RigidBodyType::Static;
        if (str == "kinematic") return components::RigidBodyType::Kinematic;
        return components::RigidBodyType::Dynamic;
    }

    std::string SceneSerialization::colliderShapeToString(components::ColliderShape shape)
    {
        switch (shape)
        {
        case components::ColliderShape::Sphere: return "sphere";
        case components::ColliderShape::Capsule: return "capsule";
        case components::ColliderShape::ConvexMesh: return "convexMesh";
        case components::ColliderShape::TriangleMesh: return "triangleMesh";
        default: return "box";
        }
    }

    components::ColliderShape SceneSerialization::stringToColliderShape(const std::string& str)
    {
        if (str == "sphere") return components::ColliderShape::Sphere;
        if (str == "capsule") return components::ColliderShape::Capsule;
        if (str == "convexMesh") return components::ColliderShape::ConvexMesh;
        if (str == "triangleMesh") return components::ColliderShape::TriangleMesh;
        return components::ColliderShape::Box;
    }

    std::string SceneSerialization::shadowQualityToString(types::ShadowQuality quality)
    {
        switch (quality)
        {
        case types::ShadowQuality::Off: return "off";
        case types::ShadowQuality::Low: return "low";
        case types::ShadowQuality::Medium: return "medium";
        case types::ShadowQuality::High: return "high";
        case types::ShadowQuality::Ultra: return "ultra";
        default: return "high";
        }
    }

    types::ShadowQuality SceneSerialization::stringToShadowQuality(const std::string& str)
    {
        if (str == "off") return types::ShadowQuality::Off;
        if (str == "low") return types::ShadowQuality::Low;
        if (str == "medium") return types::ShadowQuality::Medium;
        if (str == "high") return types::ShadowQuality::High;
        if (str == "ultra") return types::ShadowQuality::Ultra;
        return types::ShadowQuality::High;
    }

    std::string SceneSerialization::cascadeSplitModeToString(types::CascadeSplitMode mode)
    {
        switch (mode)
        {
        case types::CascadeSplitMode::Linear: return "linear";
        case types::CascadeSplitMode::Logarithmic: return "logarithmic";
        case types::CascadeSplitMode::Practical: return "practical";
        default: return "practical";
        }
    }

    types::CascadeSplitMode SceneSerialization::stringToCascadeSplitMode(const std::string& str)
    {
        if (str == "linear") return types::CascadeSplitMode::Linear;
        if (str == "logarithmic") return types::CascadeSplitMode::Logarithmic;
        if (str == "practical") return types::CascadeSplitMode::Practical;
        return types::CascadeSplitMode::Practical;
    }

    std::string SceneSerialization::toneMappingModeToString(postprocess::ToneMappingMode mode)
    {
        switch (mode)
        {
        case postprocess::ToneMappingMode::ACES: return "aces";
        case postprocess::ToneMappingMode::Reinhard: return "reinhard";
        case postprocess::ToneMappingMode::Uncharted2: return "uncharted2";
        case postprocess::ToneMappingMode::Linear: return "linear";
        default: return "aces";
        }
    }

    postprocess::ToneMappingMode SceneSerialization::stringToToneMappingMode(const std::string& str)
    {
        if (str == "aces") return postprocess::ToneMappingMode::ACES;
        if (str == "reinhard") return postprocess::ToneMappingMode::Reinhard;
        if (str == "uncharted2") return postprocess::ToneMappingMode::Uncharted2;
        if (str == "linear") return postprocess::ToneMappingMode::Linear;
        return postprocess::ToneMappingMode::ACES;
    }

    std::string SceneSerialization::fxaaQualityToString(postprocess::FXAAQuality quality)
    {
        switch (quality)
        {
        case postprocess::FXAAQuality::Low: return "low";
        case postprocess::FXAAQuality::Medium: return "medium";
        case postprocess::FXAAQuality::High: return "high";
        default: return "medium";
        }
    }

    postprocess::FXAAQuality SceneSerialization::stringToFXAAQuality(const std::string& str)
    {
        if (str == "low") return postprocess::FXAAQuality::Low;
        if (str == "medium") return postprocess::FXAAQuality::Medium;
        if (str == "high") return postprocess::FXAAQuality::High;
        return postprocess::FXAAQuality::Medium;
    }

    json SceneSerialization::serializeCollider(const components::ColliderComponent& collider)
    {
        json j;
        j["shape"] = colliderShapeToString(collider.shape);
        j["size"] = json::array({collider.size.x, collider.size.y, collider.size.z});
        j["height"] = collider.height;
        j["offset"] = json::array({collider.offset.x, collider.offset.y, collider.offset.z});
        std::string cleanPath = collider.meshPath;
        cleanNullTerminators(cleanPath);
        j["meshPath"] = cleanPath;
        j["isTrigger"] = collider.isTrigger;
        j["collisionLayer"] = collider.collisionLayer;
        j["friction"] = collider.friction;
        j["restitution"] = collider.restitution;
        return j;
    }

    void SceneSerialization::deserializeCollider(const json& j, components::ColliderComponent& collider)
    {
        if (auto it = j.find("shape"); it != j.end() && it->is_string())
        {
            collider.shape = stringToColliderShape(it->get<std::string>());
        }
        if (auto it = j.find("size"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            collider.size = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("height"); it != j.end() && it->is_number())
        {
            collider.height = it->get<float>();
        }
        if (auto it = j.find("offset"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            collider.offset = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("meshPath"); it != j.end() && it->is_string())
        {
            collider.meshPath = it->get<std::string>();
        }
        if (auto it = j.find("isTrigger"); it != j.end() && it->is_boolean())
        {
            collider.isTrigger = it->get<bool>();
        }
        if (auto it = j.find("collisionLayer"); it != j.end() && it->is_number_unsigned())
        {
            uint8_t layer = it->get<uint8_t>();
            collider.collisionLayer = layer < 16 ? layer : 1;
        }
        if (auto it = j.find("friction"); it != j.end() && it->is_number())
        {
            collider.friction = it->get<float>();
        }
        if (auto it = j.find("restitution"); it != j.end() && it->is_number())
        {
            collider.restitution = it->get<float>();
        }
    }

    json SceneSerialization::serializeRigidBody(const components::RigidBodyComponent& rigidBody)
    {
        json j;
        j["type"] = rigidBodyTypeToString(rigidBody.type);
        j["mass"] = rigidBody.mass;
        j["linearDamping"] = rigidBody.linearDamping;
        j["angularDamping"] = rigidBody.angularDamping;
        j["freezePositionX"] = rigidBody.freezePositionX;
        j["freezePositionY"] = rigidBody.freezePositionY;
        j["freezePositionZ"] = rigidBody.freezePositionZ;
        j["freezeRotationX"] = rigidBody.freezeRotationX;
        j["freezeRotationY"] = rigidBody.freezeRotationY;
        j["freezeRotationZ"] = rigidBody.freezeRotationZ;
        return j;
    }

    void SceneSerialization::deserializeRigidBody(const json& j, components::RigidBodyComponent& rigidBody)
    {
        if (auto it = j.find("type"); it != j.end() && it->is_string())
        {
            rigidBody.type = stringToRigidBodyType(it->get<std::string>());
        }
        if (auto it = j.find("mass"); it != j.end() && it->is_number())
        {
            rigidBody.mass = it->get<float>();
        }
        if (auto it = j.find("linearDamping"); it != j.end() && it->is_number())
        {
            rigidBody.linearDamping = it->get<float>();
        }
        if (auto it = j.find("angularDamping"); it != j.end() && it->is_number())
        {
            rigidBody.angularDamping = it->get<float>();
        }
        if (auto it = j.find("freezePositionX"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezePositionX = it->get<bool>();
        }
        if (auto it = j.find("freezePositionY"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezePositionY = it->get<bool>();
        }
        if (auto it = j.find("freezePositionZ"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezePositionZ = it->get<bool>();
        }
        if (auto it = j.find("freezeRotationX"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezeRotationX = it->get<bool>();
        }
        if (auto it = j.find("freezeRotationY"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezeRotationY = it->get<bool>();
        }
        if (auto it = j.find("freezeRotationZ"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezeRotationZ = it->get<bool>();
        }
    }

    json SceneSerialization::serializeVFX(const components::VFXComponent& vfx)
    {
        json j;
        std::string cleanPath = vfx.vfxPath;
        cleanNullTerminators(cleanPath);
        j["vfxPath"] = cleanPath;
        j["autoPlay"] = vfx.autoPlay;
        j["loop"] = vfx.loop;
        return j;
    }

    void SceneSerialization::deserializeVFX(const json& j, components::VFXComponent& vfx)
    {
        if (auto it = j.find("vfxPath"); it != j.end() && it->is_string())
        {
            vfx.vfxPath = it->get<std::string>();
        }
        if (auto it = j.find("autoPlay"); it != j.end() && it->is_boolean())
        {
            vfx.autoPlay = it->get<bool>();
        }
        if (auto it = j.find("loop"); it != j.end() && it->is_boolean())
        {
            vfx.loop = it->get<bool>();
        }
        // Reset runtime state
        vfx.runtimeInstanceId = 0;
        vfx.isPlaying = false;
    }

    json SceneSerialization::serializeDirectionalLight(const components::DirectionalLightComponent& light)
    {
        json j;
        j["color"] = json::array({light.color.r, light.color.g, light.color.b});
        j["intensity"] = light.intensity;
        j["showGizmo"] = light.showGizmo;
        return j;
    }

    void SceneSerialization::deserializeDirectionalLight(const json& j, components::DirectionalLightComponent& light)
    {
        if (auto it = j.find("color"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            light.color = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("intensity"); it != j.end() && it->is_number())
        {
            light.intensity = it->get<float>();
        }
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            light.showGizmo = it->get<bool>();
        }
    }

    json SceneSerialization::serializePointLight(const components::PointLightComponent& light)
    {
        json j;
        j["color"] = json::array({light.color.r, light.color.g, light.color.b});
        j["intensity"] = light.intensity;
        j["radius"] = light.radius;
        j["showGizmo"] = light.showGizmo;
        return j;
    }

    void SceneSerialization::deserializePointLight(const json& j, components::PointLightComponent& light)
    {
        if (auto it = j.find("color"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            light.color = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("intensity"); it != j.end() && it->is_number())
        {
            light.intensity = it->get<float>();
        }
        if (auto it = j.find("radius"); it != j.end() && it->is_number())
        {
            light.radius = it->get<float>();
        }
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            light.showGizmo = it->get<bool>();
        }
    }

    json SceneSerialization::serializeSpotLight(const components::SpotLightComponent& light)
    {
        json j;
        j["color"] = json::array({light.color.r, light.color.g, light.color.b});
        j["intensity"] = light.intensity;
        j["innerAngle"] = light.innerAngle;
        j["outerAngle"] = light.outerAngle;
        j["range"] = light.range;
        j["showGizmo"] = light.showGizmo;
        return j;
    }

    void SceneSerialization::deserializeSpotLight(const json& j, components::SpotLightComponent& light)
    {
        if (auto it = j.find("color"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            light.color = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("intensity"); it != j.end() && it->is_number())
        {
            light.intensity = it->get<float>();
        }
        if (auto it = j.find("innerAngle"); it != j.end() && it->is_number())
        {
            light.innerAngle = it->get<float>();
        }
        if (auto it = j.find("outerAngle"); it != j.end() && it->is_number())
        {
            light.outerAngle = it->get<float>();
        }
        if (auto it = j.find("range"); it != j.end() && it->is_number())
        {
            light.range = it->get<float>();
        }
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            light.showGizmo = it->get<bool>();
        }
    }

    json SceneSerialization::serializePhysicsSettings(const types::PhysicsSettings& settings)
    {
        json j;

        j["gravity"] = json::array({settings.gravity.x, settings.gravity.y, settings.gravity.z});
        j["gravityScale"] = settings.gravityScale;

        j["simulation"] = {
            {"fixedTimestep", settings.fixedTimestep},
            {"maxAccumulator", settings.maxAccumulator},
            {"maxStepsPerFrame", settings.maxStepsPerFrame}
        };

        j["sleepThresholds"] = {
            {"linearVelocity", settings.linearSleepThreshold},
            {"angularVelocity", settings.angularSleepThreshold},
            {"timeToSleep", settings.timeToSleep}
        };

        j["collisionLayers"] = json::array();
        for (const auto& layer : settings.layers)
        {
            j["collisionLayers"].push_back({
                {"index", layer.index},
                {"name", layer.name},
                {"builtIn", layer.isBuiltIn}
            });
        }

        j["collisionMatrix"] = json::array();
        for (size_t i = 0; i < settings.layers.size(); ++i)
        {
            json row = json::array();
            for (size_t k = 0; k < settings.layers.size(); ++k)
            {
                row.push_back(settings.collisionMatrix[i].test(k));
            }
            j["collisionMatrix"].push_back(row);
        }

        return j;
    }

    void SceneSerialization::deserializePhysicsSettings(const json& j, types::PhysicsSettings& settings)
    {
        if (j.contains("gravity") && j["gravity"].is_array() && j["gravity"].size() == 3)
        {
            settings.gravity.x = j["gravity"][0].get<float>();
            settings.gravity.y = j["gravity"][1].get<float>();
            settings.gravity.z = j["gravity"][2].get<float>();
        }
        if (j.contains("gravityScale"))
        {
            settings.gravityScale = j["gravityScale"].get<float>();
        }

        if (j.contains("simulation"))
        {
            const auto& sim = j["simulation"];
            if (sim.contains("fixedTimestep"))
                settings.fixedTimestep = sim["fixedTimestep"].get<double>();
            if (sim.contains("maxAccumulator"))
                settings.maxAccumulator = sim["maxAccumulator"].get<double>();
            if (sim.contains("maxStepsPerFrame"))
                settings.maxStepsPerFrame = sim["maxStepsPerFrame"].get<int>();
        }

        if (j.contains("sleepThresholds"))
        {
            const auto& sleep = j["sleepThresholds"];
            if (sleep.contains("linearVelocity"))
                settings.linearSleepThreshold = sleep["linearVelocity"].get<float>();
            if (sleep.contains("angularVelocity"))
                settings.angularSleepThreshold = sleep["angularVelocity"].get<float>();
            if (sleep.contains("timeToSleep"))
                settings.timeToSleep = sleep["timeToSleep"].get<float>();
        }

        if (j.contains("collisionLayers") && j["collisionLayers"].is_array())
        {
            settings.layers.clear();
            for (const auto& layerJson : j["collisionLayers"])
            {
                types::CollisionLayer layer;
                if (layerJson.contains("index"))
                    layer.index = layerJson["index"].get<uint8_t>();
                if (layerJson.contains("name"))
                    layer.name = layerJson["name"].get<std::string>();
                if (layerJson.contains("builtIn"))
                    layer.isBuiltIn = layerJson["builtIn"].get<bool>();
                settings.layers.push_back(layer);
            }
        }

        if (j.contains("collisionMatrix") && j["collisionMatrix"].is_array())
        {
            for (auto& row : settings.collisionMatrix)
            {
                row.reset();
            }

            const auto& matrix = j["collisionMatrix"];
            for (size_t i = 0; i < matrix.size() && i < types::PhysicsSettings::MAX_LAYERS; ++i)
            {
                if (matrix[i].is_array())
                {
                    for (size_t k = 0; k < matrix[i].size() && k < types::PhysicsSettings::MAX_LAYERS; ++k)
                    {
                        if (matrix[i][k].is_boolean() && matrix[i][k].get<bool>())
                        {
                            settings.collisionMatrix[i].set(k);
                        }
                    }
                }
            }
        }
    }

    std::string SceneSerialization::audioDistanceModelToString(types::AudioDistanceModel model)
    {
        switch (model)
        {
        case types::AudioDistanceModel::None: return "none";
        case types::AudioDistanceModel::InverseDistance: return "inverseDistance";
        case types::AudioDistanceModel::InverseDistanceClamped: return "inverseDistanceClamped";
        case types::AudioDistanceModel::LinearDistance: return "linearDistance";
        case types::AudioDistanceModel::LinearDistanceClamped: return "linearDistanceClamped";
        case types::AudioDistanceModel::ExponentDistance: return "exponentDistance";
        case types::AudioDistanceModel::ExponentDistanceClamped: return "exponentDistanceClamped";
        default: return "inverseDistanceClamped";
        }
    }

    types::AudioDistanceModel SceneSerialization::stringToAudioDistanceModel(const std::string& str)
    {
        if (str == "none") return types::AudioDistanceModel::None;
        if (str == "inverseDistance") return types::AudioDistanceModel::InverseDistance;
        if (str == "inverseDistanceClamped") return types::AudioDistanceModel::InverseDistanceClamped;
        if (str == "linearDistance") return types::AudioDistanceModel::LinearDistance;
        if (str == "linearDistanceClamped") return types::AudioDistanceModel::LinearDistanceClamped;
        if (str == "exponentDistance") return types::AudioDistanceModel::ExponentDistance;
        if (str == "exponentDistanceClamped") return types::AudioDistanceModel::ExponentDistanceClamped;
        return types::AudioDistanceModel::InverseDistanceClamped;
    }

    json SceneSerialization::serializeAudioSettings(const types::AudioSettings& settings)
    {
        json j;

        j["listener"] = {
            {"masterVolume", settings.masterVolume},
            {"dopplerFactor", settings.dopplerFactor},
            {"speedOfSound", settings.speedOfSound}
        };

        j["distanceModel"] = {
            {"model", audioDistanceModelToString(settings.distanceModel)},
            {"defaultRolloffFactor", settings.defaultRolloffFactor}
        };

        return j;
    }

    void SceneSerialization::deserializeAudioSettings(const json& j, types::AudioSettings& settings)
    {
        if (j.contains("listener") && j["listener"].is_object())
        {
            const auto& listener = j["listener"];
            if (listener.contains("masterVolume") && listener["masterVolume"].is_number())
                settings.masterVolume = listener["masterVolume"].get<float>();
            if (listener.contains("dopplerFactor") && listener["dopplerFactor"].is_number())
                settings.dopplerFactor = listener["dopplerFactor"].get<float>();
            if (listener.contains("speedOfSound") && listener["speedOfSound"].is_number())
                settings.speedOfSound = listener["speedOfSound"].get<float>();
        }

        if (j.contains("distanceModel") && j["distanceModel"].is_object())
        {
            const auto& dm = j["distanceModel"];
            if (dm.contains("model") && dm["model"].is_string())
                settings.distanceModel = stringToAudioDistanceModel(dm["model"].get<std::string>());
            if (dm.contains("defaultRolloffFactor") && dm["defaultRolloffFactor"].is_number())
                settings.defaultRolloffFactor = dm["defaultRolloffFactor"].get<float>();
        }
    }

    json SceneSerialization::serializeRenderSettings(const types::RenderSettings& settings)
    {
        json j;

        j["shadows"] = {
            {"enabled", settings.shadows.enabled},
            {"quality", shadowQualityToString(settings.shadows.quality)},
            {"cascadeCount", settings.shadows.cascadeCount},
            {"cascadeSplitMode", cascadeSplitModeToString(settings.shadows.cascadeSplitMode)},
            {"shadowBias", settings.shadows.shadowBias},
            {"slopeBias", settings.shadows.slopeBias},
            {"normalBias", settings.shadows.normalBias},
            {"shadowIntensity", settings.shadows.shadowIntensity}
        };

        j["culling"] = {
            {"frustumCullingEnabled", settings.culling.frustumCullingEnabled},
            {"occlusionCullingEnabled", settings.culling.occlusionCullingEnabled},
            {"lodSelectionEnabled", settings.culling.lodSelectionEnabled},
            {"meshletFrustumCullingEnabled", settings.culling.meshletFrustumCullingEnabled},
            {"meshletBackfaceCullingEnabled", settings.culling.meshletBackfaceCullingEnabled},
            {"terrainFrustumCullingEnabled", settings.culling.terrainFrustumCullingEnabled},
            {"terrainMeshletCullingEnabled", settings.culling.terrainMeshletCullingEnabled}
        };

        j["terrain"] = {
            {"enabled", settings.terrain.enabled},
            {"lodBias", settings.terrain.lodBias},
            {"errorThreshold", settings.terrain.errorThreshold},
            {"textureScale", settings.terrain.textureScale},
            {"shadowLOD", settings.terrain.shadowLOD}
        };

        j["postProcess"] = serializePostProcessSettings(settings.postProcess);

        return j;
    }

    void SceneSerialization::deserializeRenderSettings(const json& j, types::RenderSettings& settings)
    {
        if (j.contains("shadows") && j["shadows"].is_object())
        {
            const auto& shadows = j["shadows"];
            if (shadows.contains("enabled") && shadows["enabled"].is_boolean())
                settings.shadows.enabled = shadows["enabled"].get<bool>();
            if (shadows.contains("quality") && shadows["quality"].is_string())
                settings.shadows.quality = stringToShadowQuality(shadows["quality"].get<std::string>());
            if (shadows.contains("cascadeCount") && shadows["cascadeCount"].is_number_unsigned())
            {
                uint8_t count = shadows["cascadeCount"].get<uint8_t>();
                settings.shadows.cascadeCount = std::clamp(count, uint8_t(1), uint8_t(4));
            }
            if (shadows.contains("cascadeSplitMode") && shadows["cascadeSplitMode"].is_string())
                settings.shadows.cascadeSplitMode = stringToCascadeSplitMode(shadows["cascadeSplitMode"].get<std::string>());
            if (shadows.contains("shadowBias") && shadows["shadowBias"].is_number())
                settings.shadows.shadowBias = shadows["shadowBias"].get<float>();
            if (shadows.contains("slopeBias") && shadows["slopeBias"].is_number())
                settings.shadows.slopeBias = shadows["slopeBias"].get<float>();
            if (shadows.contains("normalBias") && shadows["normalBias"].is_number())
                settings.shadows.normalBias = shadows["normalBias"].get<float>();
            if (shadows.contains("shadowIntensity") && shadows["shadowIntensity"].is_number())
                settings.shadows.shadowIntensity = shadows["shadowIntensity"].get<float>();
        }

        if (j.contains("culling") && j["culling"].is_object())
        {
            const auto& culling = j["culling"];
            if (culling.contains("frustumCullingEnabled") && culling["frustumCullingEnabled"].is_boolean())
                settings.culling.frustumCullingEnabled = culling["frustumCullingEnabled"].get<bool>();
            if (culling.contains("occlusionCullingEnabled") && culling["occlusionCullingEnabled"].is_boolean())
                settings.culling.occlusionCullingEnabled = culling["occlusionCullingEnabled"].get<bool>();
            if (culling.contains("lodSelectionEnabled") && culling["lodSelectionEnabled"].is_boolean())
                settings.culling.lodSelectionEnabled = culling["lodSelectionEnabled"].get<bool>();
            if (culling.contains("meshletFrustumCullingEnabled") && culling["meshletFrustumCullingEnabled"].is_boolean())
                settings.culling.meshletFrustumCullingEnabled = culling["meshletFrustumCullingEnabled"].get<bool>();
            if (culling.contains("meshletBackfaceCullingEnabled") && culling["meshletBackfaceCullingEnabled"].is_boolean())
                settings.culling.meshletBackfaceCullingEnabled = culling["meshletBackfaceCullingEnabled"].get<bool>();
            if (culling.contains("terrainFrustumCullingEnabled") && culling["terrainFrustumCullingEnabled"].is_boolean())
                settings.culling.terrainFrustumCullingEnabled = culling["terrainFrustumCullingEnabled"].get<bool>();
            if (culling.contains("terrainMeshletCullingEnabled") && culling["terrainMeshletCullingEnabled"].is_boolean())
                settings.culling.terrainMeshletCullingEnabled = culling["terrainMeshletCullingEnabled"].get<bool>();
        }

        if (j.contains("terrain") && j["terrain"].is_object())
        {
            const auto& terrain = j["terrain"];
            if (terrain.contains("enabled") && terrain["enabled"].is_boolean())
                settings.terrain.enabled = terrain["enabled"].get<bool>();
            if (terrain.contains("lodBias") && terrain["lodBias"].is_number())
                settings.terrain.lodBias = terrain["lodBias"].get<float>();
            if (terrain.contains("errorThreshold") && terrain["errorThreshold"].is_number())
                settings.terrain.errorThreshold = terrain["errorThreshold"].get<float>();
            if (terrain.contains("textureScale") && terrain["textureScale"].is_number())
                settings.terrain.textureScale = terrain["textureScale"].get<float>();
            if (terrain.contains("shadowLOD") && terrain["shadowLOD"].is_number_unsigned())
                settings.terrain.shadowLOD = std::min(terrain["shadowLOD"].get<uint32_t>(), 3u);
        }
        else
        {
            // Initialize terrain settings with defaults for old scene files
            settings.terrain = types::TerrainSettings{};
        }

        if (j.contains("postProcess") && j["postProcess"].is_object())
        {
            deserializePostProcessSettings(j["postProcess"], settings.postProcess);
        }
        else
        {
            settings.postProcess = postprocess::PostProcessSettings::createDefault();
        }
    }

    json SceneSerialization::serializePostProcessSettings(const postprocess::PostProcessSettings& settings)
    {
        json j;

        j["enabled"] = settings.enabled;

        j["toneMapping"] = {
            {"enabled", settings.toneMapping.enabled},
            {"mode", toneMappingModeToString(settings.toneMapping.mode)},
            {"exposure", settings.toneMapping.exposure},
            {"gamma", settings.toneMapping.gamma},
            {"contrast", settings.toneMapping.contrast}
        };

        j["fxaa"] = {
            {"enabled", settings.fxaa.enabled},
            {"quality", fxaaQualityToString(settings.fxaa.quality)},
            {"edgeThresholdMin", settings.fxaa.edgeThresholdMin},
            {"edgeThreshold", settings.fxaa.edgeThreshold}
        };

        j["bloom"] = {
            {"enabled", settings.bloom.enabled},
            {"threshold", settings.bloom.threshold},
            {"intensity", settings.bloom.intensity},
            {"radius", settings.bloom.radius},
            {"passes", settings.bloom.passes}
        };

        j["vignette"] = {
            {"enabled", settings.vignette.enabled},
            {"intensity", settings.vignette.intensity},
            {"radius", settings.vignette.radius},
            {"softness", settings.vignette.softness}
        };

        j["chromaticAberration"] = {
            {"enabled", settings.chromaticAberration.enabled},
            {"intensity", settings.chromaticAberration.intensity}
        };

        j["filmGrain"] = {
            {"enabled", settings.filmGrain.enabled},
            {"intensity", settings.filmGrain.intensity},
            {"size", settings.filmGrain.size}
        };

        j["godRays"] = {
            {"enabled", settings.godRays.enabled},
            {"intensity", settings.godRays.intensity},
            {"decay", settings.godRays.decay},
            {"density", settings.godRays.density},
            {"weight", settings.godRays.weight},
            {"sampleCount", settings.godRays.sampleCount},
            {"threshold", settings.godRays.threshold}
        };

        j["depthOfField"] = {
            {"enabled", settings.depthOfField.enabled},
            {"focusMode", static_cast<int>(settings.depthOfField.focusMode)},
            {"focalDistance", settings.depthOfField.focalDistance},
            {"focusTargetX", settings.depthOfField.focusTargetX},
            {"focusTargetY", settings.depthOfField.focusTargetY},
            {"focusTargetZ", settings.depthOfField.focusTargetZ},
            {"focusSmoothing", settings.depthOfField.focusSmoothing},
            {"focalRange", settings.depthOfField.focalRange},
            {"maxBlurRadius", settings.depthOfField.maxBlurRadius},
            {"sampleCount", settings.depthOfField.sampleCount}
        };

        return j;
    }

    void SceneSerialization::deserializePostProcessSettings(const json& j, postprocess::PostProcessSettings& settings)
    {
        if (j.contains("enabled") && j["enabled"].is_boolean())
            settings.enabled = j["enabled"].get<bool>();

        if (j.contains("toneMapping") && j["toneMapping"].is_object())
        {
            const auto& tm = j["toneMapping"];
            if (tm.contains("enabled") && tm["enabled"].is_boolean())
                settings.toneMapping.enabled = tm["enabled"].get<bool>();
            if (tm.contains("mode") && tm["mode"].is_string())
                settings.toneMapping.mode = stringToToneMappingMode(tm["mode"].get<std::string>());
            if (tm.contains("exposure") && tm["exposure"].is_number())
                settings.toneMapping.exposure = std::clamp(tm["exposure"].get<float>(), 0.01f, 20.0f);
            if (tm.contains("gamma") && tm["gamma"].is_number())
                settings.toneMapping.gamma = std::clamp(tm["gamma"].get<float>(), 0.1f, 5.0f);
            if (tm.contains("contrast") && tm["contrast"].is_number())
                settings.toneMapping.contrast = std::clamp(tm["contrast"].get<float>(), 0.5f, 2.0f);
        }

        if (j.contains("fxaa") && j["fxaa"].is_object())
        {
            const auto& fxaa = j["fxaa"];
            if (fxaa.contains("enabled") && fxaa["enabled"].is_boolean())
                settings.fxaa.enabled = fxaa["enabled"].get<bool>();
            if (fxaa.contains("quality") && fxaa["quality"].is_string())
                settings.fxaa.quality = stringToFXAAQuality(fxaa["quality"].get<std::string>());
            if (fxaa.contains("edgeThresholdMin") && fxaa["edgeThresholdMin"].is_number())
                settings.fxaa.edgeThresholdMin = std::clamp(fxaa["edgeThresholdMin"].get<float>(), 0.0f, 1.0f);
            if (fxaa.contains("edgeThreshold") && fxaa["edgeThreshold"].is_number())
                settings.fxaa.edgeThreshold = std::clamp(fxaa["edgeThreshold"].get<float>(), 0.0f, 1.0f);
        }

        if (j.contains("bloom") && j["bloom"].is_object())
        {
            const auto& bloom = j["bloom"];
            if (bloom.contains("enabled") && bloom["enabled"].is_boolean())
                settings.bloom.enabled = bloom["enabled"].get<bool>();
            if (bloom.contains("threshold") && bloom["threshold"].is_number())
                settings.bloom.threshold = std::clamp(bloom["threshold"].get<float>(), 0.0f, 10.0f);
            if (bloom.contains("intensity") && bloom["intensity"].is_number())
                settings.bloom.intensity = std::clamp(bloom["intensity"].get<float>(), 0.0f, 5.0f);
            if (bloom.contains("radius") && bloom["radius"].is_number())
                settings.bloom.radius = std::clamp(bloom["radius"].get<float>(), 0.0f, 1.0f);
            if (bloom.contains("passes") && bloom["passes"].is_number_unsigned())
                settings.bloom.passes = std::clamp(bloom["passes"].get<uint32_t>(), 1u, 10u);
        }

        if (j.contains("vignette") && j["vignette"].is_object())
        {
            const auto& vignette = j["vignette"];
            if (vignette.contains("enabled") && vignette["enabled"].is_boolean())
                settings.vignette.enabled = vignette["enabled"].get<bool>();
            if (vignette.contains("intensity") && vignette["intensity"].is_number())
                settings.vignette.intensity = std::clamp(vignette["intensity"].get<float>(), 0.0f, 1.0f);
            if (vignette.contains("radius") && vignette["radius"].is_number())
                settings.vignette.radius = std::clamp(vignette["radius"].get<float>(), 0.0f, 2.0f);
            if (vignette.contains("softness") && vignette["softness"].is_number())
                settings.vignette.softness = std::clamp(vignette["softness"].get<float>(), 0.0f, 1.0f);
        }

        if (j.contains("chromaticAberration") && j["chromaticAberration"].is_object())
        {
            const auto& ca = j["chromaticAberration"];
            if (ca.contains("enabled") && ca["enabled"].is_boolean())
                settings.chromaticAberration.enabled = ca["enabled"].get<bool>();
            if (ca.contains("intensity") && ca["intensity"].is_number())
                settings.chromaticAberration.intensity = std::clamp(ca["intensity"].get<float>(), 0.0f, 0.1f);
        }

        if (j.contains("filmGrain") && j["filmGrain"].is_object())
        {
            const auto& fg = j["filmGrain"];
            if (fg.contains("enabled") && fg["enabled"].is_boolean())
                settings.filmGrain.enabled = fg["enabled"].get<bool>();
            if (fg.contains("intensity") && fg["intensity"].is_number())
                settings.filmGrain.intensity = std::clamp(fg["intensity"].get<float>(), 0.0f, 1.0f);
            if (fg.contains("size") && fg["size"].is_number())
                settings.filmGrain.size = std::clamp(fg["size"].get<float>(), 0.1f, 5.0f);
        }

        if (j.contains("godRays") && j["godRays"].is_object())
        {
            const auto& gr = j["godRays"];
            if (gr.contains("enabled") && gr["enabled"].is_boolean())
                settings.godRays.enabled = gr["enabled"].get<bool>();
            if (gr.contains("intensity") && gr["intensity"].is_number())
                settings.godRays.intensity = std::clamp(gr["intensity"].get<float>(), 0.0f, 2.0f);
            if (gr.contains("decay") && gr["decay"].is_number())
                settings.godRays.decay = std::clamp(gr["decay"].get<float>(), 0.9f, 1.0f);
            if (gr.contains("density") && gr["density"].is_number())
                settings.godRays.density = std::clamp(gr["density"].get<float>(), 0.1f, 2.0f);
            if (gr.contains("weight") && gr["weight"].is_number())
                settings.godRays.weight = std::clamp(gr["weight"].get<float>(), 0.0f, 2.0f);
            if (gr.contains("sampleCount") && gr["sampleCount"].is_number_integer())
                settings.godRays.sampleCount = std::clamp(gr["sampleCount"].get<int>(), 16, 128);
            if (gr.contains("threshold") && gr["threshold"].is_number())
                settings.godRays.threshold = std::clamp(gr["threshold"].get<float>(), 0.0f, 1.0f);
        }

        if (j.contains("depthOfField") && j["depthOfField"].is_object())
        {
            const auto& df = j["depthOfField"];
            if (df.contains("enabled") && df["enabled"].is_boolean())
                settings.depthOfField.enabled = df["enabled"].get<bool>();
            if (df.contains("focusMode") && df["focusMode"].is_number_integer())
                settings.depthOfField.focusMode = static_cast<postprocess::DoFFocusMode>(
                    std::clamp(df["focusMode"].get<int>(), 0, 1));
            if (df.contains("focalDistance") && df["focalDistance"].is_number())
                settings.depthOfField.focalDistance = std::clamp(df["focalDistance"].get<float>(), 0.1f, 1000.0f);
            if (df.contains("focusTargetX") && df["focusTargetX"].is_number())
                settings.depthOfField.focusTargetX = df["focusTargetX"].get<float>();
            if (df.contains("focusTargetY") && df["focusTargetY"].is_number())
                settings.depthOfField.focusTargetY = df["focusTargetY"].get<float>();
            if (df.contains("focusTargetZ") && df["focusTargetZ"].is_number())
                settings.depthOfField.focusTargetZ = df["focusTargetZ"].get<float>();
            if (df.contains("focusSmoothing") && df["focusSmoothing"].is_number())
                settings.depthOfField.focusSmoothing = std::clamp(df["focusSmoothing"].get<float>(), 0.1f, 50.0f);
            if (df.contains("focalRange") && df["focalRange"].is_number())
                settings.depthOfField.focalRange = std::clamp(df["focalRange"].get<float>(), 0.1f, 100.0f);
            if (df.contains("maxBlurRadius") && df["maxBlurRadius"].is_number())
                settings.depthOfField.maxBlurRadius = std::clamp(df["maxBlurRadius"].get<float>(), 0.0f, 20.0f);
            if (df.contains("sampleCount") && df["sampleCount"].is_number_integer())
                settings.depthOfField.sampleCount = std::clamp(df["sampleCount"].get<int>(), 4, 32);
        }
    }

    json SceneSerialization::serializeTerrain(const components::TerrainComponent& terrain)
    {
        json j;
        j["resolution"] = terrain.resolution;
        j["worldTileSize"] = terrain.worldTileSize;
        j["maxHeight"] = terrain.maxHeight;
        j["minHeight"] = terrain.minHeight;
        j["gridMinX"] = terrain.gridMinX;
        j["gridMinZ"] = terrain.gridMinZ;
        j["gridMaxX"] = terrain.gridMaxX;
        j["gridMaxZ"] = terrain.gridMaxZ;
        j["lodDistances"] = json::array({
            terrain.lodDistances[0], terrain.lodDistances[1],
            terrain.lodDistances[2], terrain.lodDistances[3]
        });
        std::string cleanPath = terrain.heightmapPath;
        cleanNullTerminators(cleanPath);
        j["heightmapPath"] = cleanPath;
        if (!terrain.terrainMaterialPath.empty())
        {
            std::string cleanMatPath = terrain.terrainMaterialPath;
            cleanNullTerminators(cleanMatPath);
            j["terrainMaterialPath"] = cleanMatPath;
        }
        if (!terrain.weightMapPath.empty())
        {
            std::string cleanWeightPath = terrain.weightMapPath;
            cleanNullTerminators(cleanWeightPath);
            j["weightMapPath"] = cleanWeightPath;
        }
        if (!terrain.savePath.empty())
        {
            std::string cleanSavePath = terrain.savePath;
            cleanNullTerminators(cleanSavePath);
            j["savePath"] = cleanSavePath;
        }
        // State flags
        j["isActive"] = terrain.isActive;
        return j;
    }

    void SceneSerialization::deserializeTerrain(const json& j, components::TerrainComponent& terrain)
    {
        if (auto it = j.find("resolution"); it != j.end() && it->is_number_unsigned())
            terrain.resolution = it->get<uint8_t>();
        if (auto it = j.find("worldTileSize"); it != j.end() && it->is_number())
            terrain.worldTileSize = it->get<float>();
        if (auto it = j.find("maxHeight"); it != j.end() && it->is_number())
            terrain.maxHeight = it->get<float>();
        if (auto it = j.find("minHeight"); it != j.end() && it->is_number())
            terrain.minHeight = it->get<float>();
        if (auto it = j.find("gridMinX"); it != j.end() && it->is_number_integer())
            terrain.gridMinX = it->get<int32_t>();
        if (auto it = j.find("gridMinZ"); it != j.end() && it->is_number_integer())
            terrain.gridMinZ = it->get<int32_t>();
        if (auto it = j.find("gridMaxX"); it != j.end() && it->is_number_integer())
            terrain.gridMaxX = it->get<int32_t>();
        if (auto it = j.find("gridMaxZ"); it != j.end() && it->is_number_integer())
            terrain.gridMaxZ = it->get<int32_t>();
        if (auto it = j.find("lodDistances"); it != j.end() && it->is_array() && it->size() >= 4)
        {
            terrain.lodDistances[0] = (*it)[0].get<float>();
            terrain.lodDistances[1] = (*it)[1].get<float>();
            terrain.lodDistances[2] = (*it)[2].get<float>();
            terrain.lodDistances[3] = (*it)[3].get<float>();
        }
        if (auto it = j.find("heightmapPath"); it != j.end() && it->is_string())
            terrain.heightmapPath = it->get<std::string>();
        if (auto it = j.find("terrainMaterialPath"); it != j.end() && it->is_string())
            terrain.terrainMaterialPath = it->get<std::string>();
        if (auto it = j.find("weightMapPath"); it != j.end() && it->is_string())
            terrain.weightMapPath = it->get<std::string>();
        if (auto it = j.find("savePath"); it != j.end() && it->is_string())
            terrain.savePath = it->get<std::string>();
        // State flags (with backward-compatible defaults)
        if (auto it = j.find("isActive"); it != j.end() && it->is_boolean())
            terrain.isActive = it->get<bool>();
    }

    json SceneSerialization::serializeTerrainTile(const components::TerrainTileComponent& tile)
    {
        json j;
        j["tileX"] = tile.tileX;
        j["tileZ"] = tile.tileZ;
        j["currentLOD"] = tile.currentLOD;
        j["isVisible"] = tile.isVisible;
        // State flags
        j["isDirty"] = tile.isDirty;
        j["isGPUResident"] = tile.isGPUResident;
        // Cached bounds
        j["boundingMinY"] = tile.boundingMinY;
        j["boundingMaxY"] = tile.boundingMaxY;
        return j;
    }

    void SceneSerialization::deserializeTerrainTile(const json& j, components::TerrainTileComponent& tile)
    {
        if (auto it = j.find("tileX"); it != j.end() && it->is_number_integer())
            tile.tileX = it->get<int32_t>();
        if (auto it = j.find("tileZ"); it != j.end() && it->is_number_integer())
            tile.tileZ = it->get<int32_t>();
        if (auto it = j.find("currentLOD"); it != j.end() && it->is_number_unsigned())
            tile.currentLOD = it->get<uint8_t>();
        if (auto it = j.find("isVisible"); it != j.end() && it->is_boolean())
            tile.isVisible = it->get<bool>();
        // State flags (with backward-compatible defaults)
        if (auto it = j.find("isDirty"); it != j.end() && it->is_boolean())
            tile.isDirty = it->get<bool>();
        if (auto it = j.find("isGPUResident"); it != j.end() && it->is_boolean())
            tile.isGPUResident = it->get<bool>();
        // Cached bounds
        if (auto it = j.find("boundingMinY"); it != j.end() && it->is_number())
            tile.boundingMinY = it->get<float>();
        if (auto it = j.find("boundingMaxY"); it != j.end() && it->is_number())
            tile.boundingMaxY = it->get<float>();
    }

    json SceneSerialization::serializeUICanvas(const components::UICanvasComponent& canvas)
    {
        json j;
        j["referenceWidth"] = canvas.referenceWidth;
        j["referenceHeight"] = canvas.referenceHeight;
        j["scaleMode"] = uiScaleModeToString(canvas.scaleMode);
        j["pixelsPerUnit"] = canvas.pixelsPerUnit;
        return j;
    }

    void SceneSerialization::deserializeUICanvas(const json& j, components::UICanvasComponent& canvas)
    {
        canvas.referenceWidth = j.value("referenceWidth", 1920.0f);
        canvas.referenceHeight = j.value("referenceHeight", 1080.0f);
        canvas.scaleMode = stringToUIScaleMode(j.value("scaleMode", "scaleWithScreenSize"));
        canvas.pixelsPerUnit = j.value("pixelsPerUnit", 100.0f);
    }

    json SceneSerialization::serializeUIRect(const components::UIRectComponent& rect)
    {
        json j;
        j["anchorMin"] = json::array({rect.anchorMin.x, rect.anchorMin.y});
        j["anchorMax"] = json::array({rect.anchorMax.x, rect.anchorMax.y});
        j["pivot"] = json::array({rect.pivot.x, rect.pivot.y});
        j["sizeDelta"] = json::array({rect.sizeDelta.x, rect.sizeDelta.y});
        j["anchoredPosition"] = json::array({rect.anchoredPosition.x, rect.anchoredPosition.y});
        return j;
    }

    void SceneSerialization::deserializeUIRect(const json& j, components::UIRectComponent& rect)
    {
        if (j.contains("anchorMin") && j["anchorMin"].is_array() && j["anchorMin"].size() >= 2)
        {
            rect.anchorMin = glm::vec2(j["anchorMin"][0].get<float>(), j["anchorMin"][1].get<float>());
        }
        if (j.contains("anchorMax") && j["anchorMax"].is_array() && j["anchorMax"].size() >= 2)
        {
            rect.anchorMax = glm::vec2(j["anchorMax"][0].get<float>(), j["anchorMax"][1].get<float>());
        }
        if (j.contains("pivot") && j["pivot"].is_array() && j["pivot"].size() >= 2)
        {
            rect.pivot = glm::vec2(j["pivot"][0].get<float>(), j["pivot"][1].get<float>());
        }
        if (j.contains("sizeDelta") && j["sizeDelta"].is_array() && j["sizeDelta"].size() >= 2)
        {
            rect.sizeDelta = glm::vec2(j["sizeDelta"][0].get<float>(), j["sizeDelta"][1].get<float>());
        }
        if (j.contains("anchoredPosition") && j["anchoredPosition"].is_array() && j["anchoredPosition"].size() >= 2)
        {
            rect.anchoredPosition = glm::vec2(j["anchoredPosition"][0].get<float>(), j["anchoredPosition"][1].get<float>());
        }
    }

    json SceneSerialization::serializeUIImage(const components::UIImageComponent& image)
    {
        json j;
        if (!image.texturePath.empty())
        {
            j["texturePath"] = image.texturePath;
        }
        j["colorTint"] = json::array({
            image.colorTint.r, image.colorTint.g, image.colorTint.b, image.colorTint.a
        });
        return j;
    }

    void SceneSerialization::deserializeUIImage(const json& j, components::UIImageComponent& image)
    {
        image.texturePath = j.value("texturePath", std::string(""));
        if (j.contains("colorTint") && j["colorTint"].is_array() && j["colorTint"].size() >= 4)
        {
            image.colorTint = glm::vec4(
                j["colorTint"][0].get<float>(), j["colorTint"][1].get<float>(),
                j["colorTint"][2].get<float>(), j["colorTint"][3].get<float>()
            );
        }
    }

    std::string SceneSerialization::uiScaleModeToString(components::UIScaleMode mode)
    {
        switch (mode)
        {
        case components::UIScaleMode::ConstantPixelSize: return "constantPixelSize";
        case components::UIScaleMode::ScaleWithScreenSize: return "scaleWithScreenSize";
        default: return "scaleWithScreenSize";
        }
    }

    components::UIScaleMode SceneSerialization::stringToUIScaleMode(const std::string& str)
    {
        if (str == "constantPixelSize") return components::UIScaleMode::ConstantPixelSize;
        if (str == "scaleWithScreenSize") return components::UIScaleMode::ScaleWithScreenSize;
        return components::UIScaleMode::ScaleWithScreenSize;
    }

    json SceneSerialization::serializeUIScroll(const components::UIScrollComponent& scroll)
    {
        json j;
        j["horizontalScrollEnabled"] = scroll.horizontalScrollEnabled;
        j["verticalScrollEnabled"] = scroll.verticalScrollEnabled;
        j["horizontalScrollbarVisibility"] = scrollbarVisibilityToString(scroll.horizontalScrollbarVisibility);
        j["verticalScrollbarVisibility"] = scrollbarVisibilityToString(scroll.verticalScrollbarVisibility);
        j["scrollSensitivity"] = scroll.scrollSensitivity;
        return j;
    }

    void SceneSerialization::deserializeUIScroll(const json& j, components::UIScrollComponent& scroll)
    {
        scroll.horizontalScrollEnabled = j.value("horizontalScrollEnabled", false);
        scroll.verticalScrollEnabled = j.value("verticalScrollEnabled", true);
        scroll.horizontalScrollbarVisibility = stringToScrollbarVisibility(j.value("horizontalScrollbarVisibility", "auto"));
        scroll.verticalScrollbarVisibility = stringToScrollbarVisibility(j.value("verticalScrollbarVisibility", "auto"));
        scroll.scrollSensitivity = j.value("scrollSensitivity", 1.0f);
    }

    std::string SceneSerialization::scrollbarVisibilityToString(components::ScrollbarVisibility visibility)
    {
        switch (visibility)
        {
        case components::ScrollbarVisibility::Auto: return "auto";
        case components::ScrollbarVisibility::AlwaysVisible: return "alwaysVisible";
        case components::ScrollbarVisibility::Hidden: return "hidden";
        default: return "auto";
        }
    }

    components::ScrollbarVisibility SceneSerialization::stringToScrollbarVisibility(const std::string& str)
    {
        if (str == "alwaysVisible") return components::ScrollbarVisibility::AlwaysVisible;
        if (str == "hidden") return components::ScrollbarVisibility::Hidden;
        return components::ScrollbarVisibility::Auto;
    }

    json SceneSerialization::serializeUILayoutGroup(const components::UILayoutGroupComponent& layoutGroup)
    {
        json j;
        j["direction"] = layoutDirectionToString(layoutGroup.direction);
        j["spacing"] = layoutGroup.spacing;
        j["padding"] = {layoutGroup.padding.x, layoutGroup.padding.y, layoutGroup.padding.z, layoutGroup.padding.w};
        j["childAlignment"] = childAlignmentToString(layoutGroup.childAlignment);
        j["constraintCount"] = layoutGroup.constraintCount;
        return j;
    }

    void SceneSerialization::deserializeUILayoutGroup(const json& j, components::UILayoutGroupComponent& layoutGroup)
    {
        layoutGroup.direction = stringToLayoutDirection(j.value("direction", "vertical"));
        layoutGroup.spacing = j.value("spacing", 0.0f);
        if (j.contains("padding") && j["padding"].is_array() && j["padding"].size() == 4)
        {
            layoutGroup.padding.x = j["padding"][0].get<float>();
            layoutGroup.padding.y = j["padding"][1].get<float>();
            layoutGroup.padding.z = j["padding"][2].get<float>();
            layoutGroup.padding.w = j["padding"][3].get<float>();
        }
        layoutGroup.childAlignment = stringToChildAlignment(j.value("childAlignment", "start"));
        layoutGroup.constraintCount = j.value("constraintCount", 2);
    }

    std::string SceneSerialization::layoutDirectionToString(components::LayoutDirection direction)
    {
        switch (direction)
        {
        case components::LayoutDirection::Vertical: return "vertical";
        case components::LayoutDirection::Horizontal: return "horizontal";
        case components::LayoutDirection::Grid: return "grid";
        default: return "vertical";
        }
    }

    components::LayoutDirection SceneSerialization::stringToLayoutDirection(const std::string& str)
    {
        if (str == "horizontal") return components::LayoutDirection::Horizontal;
        if (str == "grid") return components::LayoutDirection::Grid;
        return components::LayoutDirection::Vertical;
    }

    std::string SceneSerialization::childAlignmentToString(components::ChildAlignment alignment)
    {
        switch (alignment)
        {
        case components::ChildAlignment::Start: return "start";
        case components::ChildAlignment::Center: return "center";
        case components::ChildAlignment::End: return "end";
        default: return "start";
        }
    }

    components::ChildAlignment SceneSerialization::stringToChildAlignment(const std::string& str)
    {
        if (str == "center") return components::ChildAlignment::Center;
        if (str == "end") return components::ChildAlignment::End;
        return components::ChildAlignment::Start;
    }

    json SceneSerialization::serializeUILabel(const components::UILabelComponent& label)
    {
        json j;
        if (!label.text.empty())
        {
            j["text"] = label.text;
        }
        if (!label.fontPath.empty())
        {
            j["fontPath"] = label.fontPath;
        }
        j["fontSize"] = label.fontSize;
        j["fontStyle"] = fontStyleToString(label.fontStyle);
        j["color"] = json::array({label.color.r, label.color.g, label.color.b, label.color.a});
        j["horizontalAlignment"] = horizontalAlignmentToString(label.horizontalAlignment);
        j["verticalAlignment"] = verticalAlignmentToString(label.verticalAlignment);
        j["overflow"] = textOverflowToString(label.overflow);
        j["wordWrap"] = label.wordWrap;
        j["lineSpacing"] = label.lineSpacing;
        j["letterSpacing"] = label.letterSpacing;
        return j;
    }

    void SceneSerialization::deserializeUILabel(const json& j, components::UILabelComponent& label)
    {
        label.text = j.value("text", std::string("Label"));
        label.fontPath = j.value("fontPath", std::string(""));
        label.fontSize = j.value("fontSize", 16.0f);
        label.fontStyle = stringToFontStyle(j.value("fontStyle", "normal"));
        if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 4)
        {
            label.color = glm::vec4(
                j["color"][0].get<float>(), j["color"][1].get<float>(),
                j["color"][2].get<float>(), j["color"][3].get<float>()
            );
        }
        label.horizontalAlignment = stringToHorizontalAlignment(j.value("horizontalAlignment", "left"));
        label.verticalAlignment = stringToVerticalAlignment(j.value("verticalAlignment", "top"));
        label.overflow = stringToTextOverflow(j.value("overflow", "overflow"));
        label.wordWrap = j.value("wordWrap", true);
        label.lineSpacing = j.value("lineSpacing", 1.0f);
        label.letterSpacing = j.value("letterSpacing", 0.0f);
    }

    json SceneSerialization::serializeUIButton(const components::UIButtonComponent& button)
    {
        json j;

        j["normalColor"] = json::array({button.normalColor.r, button.normalColor.g, button.normalColor.b, button.normalColor.a});
        j["hoveredColor"] = json::array({button.hoveredColor.r, button.hoveredColor.g, button.hoveredColor.b, button.hoveredColor.a});
        j["pressedColor"] = json::array({button.pressedColor.r, button.pressedColor.g, button.pressedColor.b, button.pressedColor.a});
        j["disabledColor"] = json::array({button.disabledColor.r, button.disabledColor.g, button.disabledColor.b, button.disabledColor.a});

        if (!button.normalTexture.empty())
        {
            j["normalTexture"] = button.normalTexture;
        }
        if (!button.hoverTexture.empty())
        {
            j["hoverTexture"] = button.hoverTexture;
        }
        if (!button.pressedTexture.empty())
        {
            j["pressedTexture"] = button.pressedTexture;
        }
        if (!button.disabledTexture.empty())
        {
            j["disabledTexture"] = button.disabledTexture;
        }

        j["colorTransitionDuration"] = button.colorTransitionDuration;
        j["interactable"] = button.interactable;

        return j;
    }

    void SceneSerialization::deserializeUIButton(const json& j, components::UIButtonComponent& button)
    {
        if (j.contains("normalColor") && j["normalColor"].is_array() && j["normalColor"].size() >= 4)
        {
            button.normalColor = glm::vec4(
                j["normalColor"][0].get<float>(), j["normalColor"][1].get<float>(),
                j["normalColor"][2].get<float>(), j["normalColor"][3].get<float>()
            );
        }
        if (j.contains("hoveredColor") && j["hoveredColor"].is_array() && j["hoveredColor"].size() >= 4)
        {
            button.hoveredColor = glm::vec4(
                j["hoveredColor"][0].get<float>(), j["hoveredColor"][1].get<float>(),
                j["hoveredColor"][2].get<float>(), j["hoveredColor"][3].get<float>()
            );
        }
        if (j.contains("pressedColor") && j["pressedColor"].is_array() && j["pressedColor"].size() >= 4)
        {
            button.pressedColor = glm::vec4(
                j["pressedColor"][0].get<float>(), j["pressedColor"][1].get<float>(),
                j["pressedColor"][2].get<float>(), j["pressedColor"][3].get<float>()
            );
        }
        if (j.contains("disabledColor") && j["disabledColor"].is_array() && j["disabledColor"].size() >= 4)
        {
            button.disabledColor = glm::vec4(
                j["disabledColor"][0].get<float>(), j["disabledColor"][1].get<float>(),
                j["disabledColor"][2].get<float>(), j["disabledColor"][3].get<float>()
            );
        }

        button.normalTexture = j.value("normalTexture", std::string(""));
        button.hoverTexture = j.value("hoverTexture", std::string(""));
        button.pressedTexture = j.value("pressedTexture", std::string(""));
        button.disabledTexture = j.value("disabledTexture", std::string(""));

        button.colorTransitionDuration = j.value("colorTransitionDuration", 0.1f);
        button.interactable = j.value("interactable", true);
    }

    json SceneSerialization::serializeUITextInput(const components::UITextInputComponent& textInput)
    {
        json j;

        j["text"] = textInput.text;
        j["placeholderText"] = textInput.placeholderText;

        if (!textInput.fontPath.empty())
        {
            j["fontPath"] = textInput.fontPath;
        }

        j["fontSize"] = textInput.fontSize;
        j["textColor"] = json::array({textInput.textColor.r, textInput.textColor.g, textInput.textColor.b, textInput.textColor.a});
        j["placeholderColor"] = json::array({textInput.placeholderColor.r, textInput.placeholderColor.g, textInput.placeholderColor.b, textInput.placeholderColor.a});
        j["normalColor"] = json::array({textInput.normalColor.r, textInput.normalColor.g, textInput.normalColor.b, textInput.normalColor.a});
        j["hoveredColor"] = json::array({textInput.hoveredColor.r, textInput.hoveredColor.g, textInput.hoveredColor.b, textInput.hoveredColor.a});
        j["focusedColor"] = json::array({textInput.focusedColor.r, textInput.focusedColor.g, textInput.focusedColor.b, textInput.focusedColor.a});
        j["disabledColor"] = json::array({textInput.disabledColor.r, textInput.disabledColor.g, textInput.disabledColor.b, textInput.disabledColor.a});

        j["colorTransitionDuration"] = textInput.colorTransitionDuration;
        j["interactable"] = textInput.interactable;
        j["maxLength"] = textInput.maxLength;

        j["selectionColor"] = json::array({textInput.selectionColor.r, textInput.selectionColor.g, textInput.selectionColor.b, textInput.selectionColor.a});
        j["caretColor"] = json::array({textInput.caretColor.r, textInput.caretColor.g, textInput.caretColor.b, textInput.caretColor.a});
        j["caretWidth"] = textInput.caretWidth;
        j["caretBlinkRate"] = textInput.caretBlinkRate;

        return j;
    }

    void SceneSerialization::deserializeUITextInput(const json& j, components::UITextInputComponent& textInput)
    {
        textInput.text = j.value("text", std::string(""));
        textInput.placeholderText = j.value("placeholderText", std::string("Enter text..."));
        textInput.fontPath = j.value("fontPath", std::string(""));
        textInput.fontSize = j.value("fontSize", 16.0f);

        auto deserializeVec4 = [&](const std::string& key, glm::vec4& out)
        {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
            {
                out = glm::vec4(
                    j[key][0].get<float>(), j[key][1].get<float>(),
                    j[key][2].get<float>(), j[key][3].get<float>()
                );
            }
        };

        deserializeVec4("textColor", textInput.textColor);
        deserializeVec4("placeholderColor", textInput.placeholderColor);
        deserializeVec4("normalColor", textInput.normalColor);
        deserializeVec4("hoveredColor", textInput.hoveredColor);
        deserializeVec4("focusedColor", textInput.focusedColor);
        deserializeVec4("disabledColor", textInput.disabledColor);
        deserializeVec4("selectionColor", textInput.selectionColor);
        deserializeVec4("caretColor", textInput.caretColor);

        textInput.colorTransitionDuration = j.value("colorTransitionDuration", 0.1f);
        textInput.interactable = j.value("interactable", true);
        textInput.maxLength = j.value("maxLength", 0);
        textInput.caretWidth = j.value("caretWidth", 2.0f);
        textInput.caretBlinkRate = j.value("caretBlinkRate", 0.53f);

        textInput.currentDisplayColor = textInput.normalColor;
    }

    json SceneSerialization::serializeUICheckbox(const components::UICheckboxComponent& checkbox)
    {
        json j;

        j["isChecked"] = checkbox.isChecked;

        if (!checkbox.groupName.empty())
        {
            j["groupName"] = checkbox.groupName;
        }
        j["allowUncheck"] = checkbox.allowUncheck;

        j["uncheckedColor"] = json::array({checkbox.uncheckedColor.r, checkbox.uncheckedColor.g, checkbox.uncheckedColor.b, checkbox.uncheckedColor.a});
        j["checkedColor"] = json::array({checkbox.checkedColor.r, checkbox.checkedColor.g, checkbox.checkedColor.b, checkbox.checkedColor.a});
        j["hoveredColor"] = json::array({checkbox.hoveredColor.r, checkbox.hoveredColor.g, checkbox.hoveredColor.b, checkbox.hoveredColor.a});
        j["disabledColor"] = json::array({checkbox.disabledColor.r, checkbox.disabledColor.g, checkbox.disabledColor.b, checkbox.disabledColor.a});

        if (!checkbox.uncheckedTexture.empty())
        {
            j["uncheckedTexture"] = checkbox.uncheckedTexture;
        }
        if (!checkbox.checkedTexture.empty())
        {
            j["checkedTexture"] = checkbox.checkedTexture;
        }
        if (!checkbox.hoveredTexture.empty())
        {
            j["hoveredTexture"] = checkbox.hoveredTexture;
        }
        if (!checkbox.disabledTexture.empty())
        {
            j["disabledTexture"] = checkbox.disabledTexture;
        }

        j["colorTransitionDuration"] = checkbox.colorTransitionDuration;
        j["interactable"] = checkbox.interactable;
        j["labelToggle"] = checkbox.labelToggle;

        return j;
    }

    void SceneSerialization::deserializeUICheckbox(const json& j, components::UICheckboxComponent& checkbox)
    {
        checkbox.isChecked = j.value("isChecked", false);
        checkbox.groupName = j.value("groupName", std::string(""));
        checkbox.allowUncheck = j.value("allowUncheck", true);

        auto deserializeVec4 = [&](const std::string& key, glm::vec4& out)
        {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
            {
                out = glm::vec4(
                    j[key][0].get<float>(), j[key][1].get<float>(),
                    j[key][2].get<float>(), j[key][3].get<float>()
                );
            }
        };

        deserializeVec4("uncheckedColor", checkbox.uncheckedColor);
        deserializeVec4("checkedColor", checkbox.checkedColor);
        deserializeVec4("hoveredColor", checkbox.hoveredColor);
        deserializeVec4("disabledColor", checkbox.disabledColor);

        checkbox.uncheckedTexture = j.value("uncheckedTexture", std::string(""));
        checkbox.checkedTexture = j.value("checkedTexture", std::string(""));
        checkbox.hoveredTexture = j.value("hoveredTexture", std::string(""));
        checkbox.disabledTexture = j.value("disabledTexture", std::string(""));

        checkbox.colorTransitionDuration = j.value("colorTransitionDuration", 0.1f);
        checkbox.interactable = j.value("interactable", true);
        checkbox.labelToggle = j.value("labelToggle", false);

        checkbox.currentDisplayColor = checkbox.isChecked ? checkbox.checkedColor : checkbox.uncheckedColor;
    }

    json SceneSerialization::serializeUIDropdown(const components::UIDropdownComponent& dropdown)
    {
        json j;

        // Options array
        json optionsArr = json::array();
        for (const auto& opt : dropdown.options)
        {
            json optJson;
            optJson["text"] = opt.text;
            if (!opt.iconPath.empty())
            {
                optJson["iconPath"] = opt.iconPath;
            }
            optionsArr.push_back(optJson);
        }
        j["options"] = optionsArr;

        j["selectedIndex"] = dropdown.selectedIndex;
        j["placeholderText"] = dropdown.placeholderText;
        j["maxVisibleItems"] = dropdown.maxVisibleItems;
        j["interactable"] = dropdown.interactable;

        // Header state colors
        j["normalColor"] = json::array({dropdown.normalColor.r, dropdown.normalColor.g,
                                         dropdown.normalColor.b, dropdown.normalColor.a});
        j["hoveredColor"] = json::array({dropdown.hoveredColor.r, dropdown.hoveredColor.g,
                                          dropdown.hoveredColor.b, dropdown.hoveredColor.a});
        j["openColor"] = json::array({dropdown.openColor.r, dropdown.openColor.g,
                                       dropdown.openColor.b, dropdown.openColor.a});
        j["disabledColor"] = json::array({dropdown.disabledColor.r, dropdown.disabledColor.g,
                                           dropdown.disabledColor.b, dropdown.disabledColor.a});

        // List colors
        j["listBackgroundColor"] = json::array({dropdown.listBackgroundColor.r, dropdown.listBackgroundColor.g,
                                                 dropdown.listBackgroundColor.b, dropdown.listBackgroundColor.a});
        j["itemNormalColor"] = json::array({dropdown.itemNormalColor.r, dropdown.itemNormalColor.g,
                                             dropdown.itemNormalColor.b, dropdown.itemNormalColor.a});
        j["itemHoveredColor"] = json::array({dropdown.itemHoveredColor.r, dropdown.itemHoveredColor.g,
                                              dropdown.itemHoveredColor.b, dropdown.itemHoveredColor.a});

        // Font
        if (!dropdown.fontPath.empty())
        {
            j["fontPath"] = dropdown.fontPath;
        }
        j["fontSize"] = dropdown.fontSize;

        j["colorTransitionDuration"] = dropdown.colorTransitionDuration;

        return j;
    }

    void SceneSerialization::deserializeUIDropdown(const json& j, components::UIDropdownComponent& dropdown)
    {
        // Options array
        if (j.contains("options") && j["options"].is_array())
        {
            dropdown.options.clear();
            for (const auto& optJson : j["options"])
            {
                components::DropdownOption opt;
                opt.text = optJson.value("text", std::string(""));
                opt.iconPath = optJson.value("iconPath", std::string(""));
                dropdown.options.push_back(opt);
            }
        }

        dropdown.selectedIndex = j.value("selectedIndex", -1);
        dropdown.placeholderText = j.value("placeholderText", std::string("Select..."));
        dropdown.maxVisibleItems = j.value("maxVisibleItems", 5);
        dropdown.interactable = j.value("interactable", true);

        auto deserializeVec4 = [&](const std::string& key, glm::vec4& out)
        {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
            {
                out = glm::vec4(
                    j[key][0].get<float>(), j[key][1].get<float>(),
                    j[key][2].get<float>(), j[key][3].get<float>()
                );
            }
        };

        deserializeVec4("normalColor", dropdown.normalColor);
        deserializeVec4("hoveredColor", dropdown.hoveredColor);
        deserializeVec4("openColor", dropdown.openColor);
        deserializeVec4("disabledColor", dropdown.disabledColor);
        deserializeVec4("listBackgroundColor", dropdown.listBackgroundColor);
        deserializeVec4("itemNormalColor", dropdown.itemNormalColor);
        deserializeVec4("itemHoveredColor", dropdown.itemHoveredColor);

        dropdown.fontPath = j.value("fontPath", std::string(""));
        dropdown.fontSize = j.value("fontSize", 16.0f);
        dropdown.colorTransitionDuration = j.value("colorTransitionDuration", 0.1f);

        // Reset runtime state
        dropdown.currentDisplayColor = dropdown.normalColor;
    }

    json SceneSerialization::serializeUITabs(const components::UITabsComponent& tabs)
    {
        json j;

        std::string posStr;
        switch (tabs.tabBarPosition)
        {
        case components::TabBarPosition::Top: posStr = "Top"; break;
        case components::TabBarPosition::Bottom: posStr = "Bottom"; break;
        case components::TabBarPosition::Left: posStr = "Left"; break;
        case components::TabBarPosition::Right: posStr = "Right"; break;
        default: posStr = "Top"; break;
        }
        j["tabBarPosition"] = posStr;
        j["activeTabIndex"] = tabs.activeTabIndex;

        return j;
    }

    void SceneSerialization::deserializeUITabs(const json& j, components::UITabsComponent& tabs)
    {
        if (j.contains("tabBarPosition"))
        {
            std::string posStr = j["tabBarPosition"].get<std::string>();
            if (posStr == "Bottom") tabs.tabBarPosition = components::TabBarPosition::Bottom;
            else if (posStr == "Left") tabs.tabBarPosition = components::TabBarPosition::Left;
            else if (posStr == "Right") tabs.tabBarPosition = components::TabBarPosition::Right;
            else tabs.tabBarPosition = components::TabBarPosition::Top;
        }

        tabs.activeTabIndex = j.value("activeTabIndex", 0);

        // Reset runtime state
        tabs.previousTabIndex = -1;
    }

    json SceneSerialization::serializeUISlider(const components::UISliderComponent& slider)
    {
        json j;
        j["minValue"] = slider.minValue;
        j["maxValue"] = slider.maxValue;
        j["value"] = slider.value;
        j["stepSize"] = slider.stepSize;

        switch (slider.orientation) {
            case components::UISliderOrientation::Vertical: j["orientation"] = "Vertical"; break;
            default: j["orientation"] = "Horizontal"; break;
        }

        j["clickTrackToSet"] = slider.clickTrackToSet;
        j["handleSizeRatio"] = slider.handleSizeRatio;

        j["handleNormalColor"] = json::array({slider.handleNormalColor.r, slider.handleNormalColor.g, slider.handleNormalColor.b, slider.handleNormalColor.a});
        j["handleHoveredColor"] = json::array({slider.handleHoveredColor.r, slider.handleHoveredColor.g, slider.handleHoveredColor.b, slider.handleHoveredColor.a});
        j["handlePressedColor"] = json::array({slider.handlePressedColor.r, slider.handlePressedColor.g, slider.handlePressedColor.b, slider.handlePressedColor.a});
        j["handleDisabledColor"] = json::array({slider.handleDisabledColor.r, slider.handleDisabledColor.g, slider.handleDisabledColor.b, slider.handleDisabledColor.a});

        if (!slider.handleNormalTexture.empty()) j["handleNormalTexture"] = slider.handleNormalTexture;
        if (!slider.handleHoveredTexture.empty()) j["handleHoveredTexture"] = slider.handleHoveredTexture;
        if (!slider.handlePressedTexture.empty()) j["handlePressedTexture"] = slider.handlePressedTexture;
        if (!slider.handleDisabledTexture.empty()) j["handleDisabledTexture"] = slider.handleDisabledTexture;

        j["fillColor"] = json::array({slider.fillColor.r, slider.fillColor.g, slider.fillColor.b, slider.fillColor.a});
        if (!slider.fillTexture.empty()) j["fillTexture"] = slider.fillTexture;

        j["colorTransitionDuration"] = slider.colorTransitionDuration;
        j["interactable"] = slider.interactable;
        return j;
    }

    void SceneSerialization::deserializeUISlider(const json& j, components::UISliderComponent& slider)
    {
        slider.minValue = j.value("minValue", 0.0f);
        slider.maxValue = j.value("maxValue", 1.0f);
        slider.value = j.value("value", 0.5f);
        slider.stepSize = j.value("stepSize", 0.0f);

        std::string orientStr = j.value("orientation", std::string("Horizontal"));
        if (orientStr == "Vertical")
            slider.orientation = components::UISliderOrientation::Vertical;
        else
            slider.orientation = components::UISliderOrientation::Horizontal;

        slider.clickTrackToSet = j.value("clickTrackToSet", true);
        slider.handleSizeRatio = j.value("handleSizeRatio", 0.08f);

        auto deserializeVec4 = [&](const std::string& key, glm::vec4& out) {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
                out = glm::vec4(j[key][0].get<float>(), j[key][1].get<float>(),
                               j[key][2].get<float>(), j[key][3].get<float>());
        };

        deserializeVec4("handleNormalColor", slider.handleNormalColor);
        deserializeVec4("handleHoveredColor", slider.handleHoveredColor);
        deserializeVec4("handlePressedColor", slider.handlePressedColor);
        deserializeVec4("handleDisabledColor", slider.handleDisabledColor);

        slider.handleNormalTexture = j.value("handleNormalTexture", std::string(""));
        slider.handleHoveredTexture = j.value("handleHoveredTexture", std::string(""));
        slider.handlePressedTexture = j.value("handlePressedTexture", std::string(""));
        slider.handleDisabledTexture = j.value("handleDisabledTexture", std::string(""));

        deserializeVec4("fillColor", slider.fillColor);
        slider.fillTexture = j.value("fillTexture", std::string(""));

        slider.colorTransitionDuration = j.value("colorTransitionDuration", 0.1f);
        slider.interactable = j.value("interactable", true);

        // Reset runtime state
        slider.currentState = components::UISliderState::Normal;
        slider.currentHandleDisplayColor = slider.handleNormalColor;
        slider.isDragging = false;
        slider.dragStartValue = 0.0f;
    }

    std::string SceneSerialization::horizontalAlignmentToString(components::HorizontalAlignment alignment)
    {
        switch (alignment)
        {
        case components::HorizontalAlignment::Left: return "left";
        case components::HorizontalAlignment::Center: return "center";
        case components::HorizontalAlignment::Right: return "right";
        default: return "left";
        }
    }

    components::HorizontalAlignment SceneSerialization::stringToHorizontalAlignment(const std::string& str)
    {
        if (str == "center") return components::HorizontalAlignment::Center;
        if (str == "right") return components::HorizontalAlignment::Right;
        return components::HorizontalAlignment::Left;
    }

    std::string SceneSerialization::verticalAlignmentToString(components::VerticalAlignment alignment)
    {
        switch (alignment)
        {
        case components::VerticalAlignment::Top: return "top";
        case components::VerticalAlignment::Middle: return "middle";
        case components::VerticalAlignment::Bottom: return "bottom";
        default: return "top";
        }
    }

    components::VerticalAlignment SceneSerialization::stringToVerticalAlignment(const std::string& str)
    {
        if (str == "middle") return components::VerticalAlignment::Middle;
        if (str == "bottom") return components::VerticalAlignment::Bottom;
        return components::VerticalAlignment::Top;
    }

    std::string SceneSerialization::textOverflowToString(components::TextOverflow overflow)
    {
        switch (overflow)
        {
        case components::TextOverflow::Overflow: return "overflow";
        case components::TextOverflow::Clip: return "clip";
        case components::TextOverflow::Ellipsis: return "ellipsis";
        default: return "overflow";
        }
    }

    components::TextOverflow SceneSerialization::stringToTextOverflow(const std::string& str)
    {
        if (str == "clip") return components::TextOverflow::Clip;
        if (str == "ellipsis") return components::TextOverflow::Ellipsis;
        return components::TextOverflow::Overflow;
    }

    std::string SceneSerialization::fontStyleToString(components::FontStyle style)
    {
        switch (style)
        {
        case components::FontStyle::Normal: return "normal";
        case components::FontStyle::Bold: return "bold";
        case components::FontStyle::Italic: return "italic";
        case components::FontStyle::BoldItalic: return "boldItalic";
        default: return "normal";
        }
    }

    components::FontStyle SceneSerialization::stringToFontStyle(const std::string& str)
    {
        if (str == "bold") return components::FontStyle::Bold;
        if (str == "italic") return components::FontStyle::Italic;
        if (str == "boldItalic") return components::FontStyle::BoldItalic;
        return components::FontStyle::Normal;
    }

    void SceneSerialization::deserializeChildren(const json& childrenJson, scene::Entity& parent,
                                                 scene::SceneGraphSystem& sceneGraph,
                                                 SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded,
                                                 size_t totalEntities)
    {
        for (const auto& childJson : childrenJson)
        {
            if (!childJson.is_object())
            {
                vfLogWarning("Skipping invalid child entry in scene file (not an object)");
                continue;
            }

            std::string childName = childJson.value("name", "Unnamed");

            scene::Entity child(childName);

            if (!child.isValid())
            {
                vfLogError("Failed to create child entity '{}' during scene load", childName);
                continue;
            }

            if (childJson.contains("uuid") && childJson["uuid"].is_number_unsigned())
            {
                uint64_t uuidValue = childJson["uuid"].get<uint64_t>();
                child.addOrReplaceComponent<components::UUIDComponent>(uuidValue);
            }

            sceneGraph.addChild(parent, child);

            deserializeEntity(childJson, child, sceneGraph, false, progressCallback, entitiesLoaded, totalEntities);
        }
    }

    void SceneSerialization::deserializeEntity(const json& entityJson, scene::Entity& entity,
                                               scene::SceneGraphSystem& sceneGraph, bool isRoot,
                                               SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded,
                                               size_t totalEntities)
    {
        std::string entityName = "Unnamed";
        if (entityJson.contains("name"))
        {
            entityName = entityJson["name"].get<std::string>();
            entity.setName(entityName);
        }

        if (entityJson.contains("isActive") && entityJson["isActive"].is_boolean())
        {
            if (entity.hasComponent<components::NameComponent>())
            {
                entity.getComponent<components::NameComponent>().isActive = entityJson["isActive"].get<bool>();
            }
        }

        if (progressCallback)
        {
            progressCallback(entityName, entitiesLoaded, totalEntities);
        }
        ++entitiesLoaded;

        if (isRoot && entityJson.contains("uuid"))
        {
            uint64_t uuidValue = entityJson["uuid"].get<uint64_t>();
            entity.addOrReplaceComponent<components::UUIDComponent>(uuidValue);
        }


        if (entityJson.contains("transform"))
        {
            auto& transform = entity.getComponent<components::TransformComponent>();
            deserializeTransform(entityJson["transform"], transform);
        }

        if (entityJson.contains("components"))
        {
            const auto& componentsJson = entityJson["components"];

            if (componentsJson.contains("camera"))
            {
                auto& camera = entity.addOrReplaceComponent<components::CameraComponent>();
                deserializeCamera(componentsJson["camera"], camera);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::Camera;
                }
            }

            if (componentsJson.contains("ibl"))
            {
                std::string iblFileName = deserializeIBL(componentsJson["ibl"]);
                if (!iblFileName.empty())
                {
                    entity.addOrReplaceComponent<components::IBLComponent>().fileName = iblFileName;
                }
            }

            if (componentsJson.contains("mesh"))
            {
                auto& meshComp = entity.addOrReplaceComponent<components::MeshComponent>();
                deserializeMesh(componentsJson["mesh"], meshComp);
            }

            if (componentsJson.contains("material"))
            {
                auto& matComp = entity.addOrReplaceComponent<components::MaterialComponent>();
                deserializeMaterial(componentsJson["material"], matComp);
            }

            if (componentsJson.contains("billboard"))
            {
                auto& billboardComp = entity.addOrReplaceComponent<components::BillboardComponent>();
                deserializeBillboard(componentsJson["billboard"], billboardComp);
            }

            if (componentsJson.contains("text"))
            {
                auto& textComp = entity.addOrReplaceComponent<components::TextComponent>();
                deserializeText(componentsJson["text"], textComp);
            }

            if (componentsJson.contains("audioSource2D"))
            {
                auto& audioComp = entity.addOrReplaceComponent<components::AudioSource2DComponent>();
                deserializeAudioSource2D(componentsJson["audioSource2D"], audioComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::Audio2D;
                }
            }

            if (componentsJson.contains("audioSource3D"))
            {
                auto& audioComp = entity.addOrReplaceComponent<components::AudioSource3DComponent>();
                deserializeAudioSource3D(componentsJson["audioSource3D"], audioComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::Audio3D;
                }
            }

            if (componentsJson.contains("script"))
            {
                auto& scriptComp = entity.addOrReplaceComponent<components::ScriptComponent>();
                deserializeScript(componentsJson["script"], scriptComp);
            }

            if (componentsJson.contains("collider"))
            {
                auto& colliderComp = entity.addOrReplaceComponent<components::ColliderComponent>();
                deserializeCollider(componentsJson["collider"], colliderComp);
            }

            if (componentsJson.contains("rigidBody"))
            {
                auto& rigidBodyComp = entity.addOrReplaceComponent<components::RigidBodyComponent>();
                deserializeRigidBody(componentsJson["rigidBody"], rigidBodyComp);
            }

            if (componentsJson.contains("vfx"))
            {
                auto& vfxComp = entity.addOrReplaceComponent<components::VFXComponent>();
                deserializeVFX(componentsJson["vfx"], vfxComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::Particle;
                }
            }

            if (componentsJson.contains("directionalLight"))
            {
                auto& lightComp = entity.addOrReplaceComponent<components::DirectionalLightComponent>();
                deserializeDirectionalLight(componentsJson["directionalLight"], lightComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::DirectionalLight;
                }
            }

            if (componentsJson.contains("pointLight"))
            {
                auto& lightComp = entity.addOrReplaceComponent<components::PointLightComponent>();
                deserializePointLight(componentsJson["pointLight"], lightComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::PointLight;
                }
            }

            if (componentsJson.contains("spotLight"))
            {
                auto& lightComp = entity.addOrReplaceComponent<components::SpotLightComponent>();
                deserializeSpotLight(componentsJson["spotLight"], lightComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::SpotLight;
                }
            }

            if (componentsJson.contains("terrain"))
            {
                auto& terrainComp = entity.addOrReplaceComponent<components::TerrainComponent>();
                deserializeTerrain(componentsJson["terrain"], terrainComp);
            }

            if (componentsJson.contains("terrainTile"))
            {
                auto& tileComp = entity.addOrReplaceComponent<components::TerrainTileComponent>();
                deserializeTerrainTile(componentsJson["terrainTile"], tileComp);
            }

            if (componentsJson.contains("uiCanvas"))
            {
                auto& canvasComp = entity.addOrReplaceComponent<components::UICanvasComponent>();
                deserializeUICanvas(componentsJson["uiCanvas"], canvasComp);
            }

            if (componentsJson.contains("uiRect"))
            {
                auto& rectComp = entity.addOrReplaceComponent<components::UIRectComponent>();
                deserializeUIRect(componentsJson["uiRect"], rectComp);
            }

            if (componentsJson.contains("uiImage"))
            {
                auto& imageComp = entity.addOrReplaceComponent<components::UIImageComponent>();
                deserializeUIImage(componentsJson["uiImage"], imageComp);
            }

            if (componentsJson.contains("uiScroll"))
            {
                auto& scrollComp = entity.addOrReplaceComponent<components::UIScrollComponent>();
                deserializeUIScroll(componentsJson["uiScroll"], scrollComp);
            }

            if (componentsJson.contains("uiLayoutGroup"))
            {
                auto& layoutGroupComp = entity.addOrReplaceComponent<components::UILayoutGroupComponent>();
                deserializeUILayoutGroup(componentsJson["uiLayoutGroup"], layoutGroupComp);
            }

            if (componentsJson.contains("uiLabel"))
            {
                auto& labelComp = entity.addOrReplaceComponent<components::UILabelComponent>();
                deserializeUILabel(componentsJson["uiLabel"], labelComp);
            }

            if (componentsJson.contains("uiButton"))
            {
                auto& buttonComp = entity.addOrReplaceComponent<components::UIButtonComponent>();
                deserializeUIButton(componentsJson["uiButton"], buttonComp);
            }

            if (componentsJson.contains("uiTextInput"))
            {
                auto& textInputComp = entity.addOrReplaceComponent<components::UITextInputComponent>();
                deserializeUITextInput(componentsJson["uiTextInput"], textInputComp);
            }

            if (componentsJson.contains("uiCheckbox"))
            {
                auto& checkboxComp = entity.addOrReplaceComponent<components::UICheckboxComponent>();
                deserializeUICheckbox(componentsJson["uiCheckbox"], checkboxComp);
            }

            if (componentsJson.contains("uiDropdown"))
            {
                auto& dropdownComp = entity.addOrReplaceComponent<components::UIDropdownComponent>();
                deserializeUIDropdown(componentsJson["uiDropdown"], dropdownComp);
            }

            if (componentsJson.contains("uiTabs"))
            {
                auto& tabsComp = entity.addOrReplaceComponent<components::UITabsComponent>();
                deserializeUITabs(componentsJson["uiTabs"], tabsComp);
            }

            if (componentsJson.contains("uiSlider"))
            {
                auto& sliderComp = entity.addOrReplaceComponent<components::UISliderComponent>();
                deserializeUISlider(componentsJson["uiSlider"], sliderComp);
            }
        }

        if (entityJson.contains("children") && entityJson["children"].is_array())
        {
            deserializeChildren(entityJson["children"], entity, sceneGraph, progressCallback, entitiesLoaded,
                                totalEntities);
        }
    }

    scene::SceneGraphSystem SceneSerialization::loadScene(std::string_view filename)
    {
        scene::SceneGraphSystem sceneGraph;
        loadSceneInto(filename, sceneGraph);
        return sceneGraph;
    }

    size_t SceneSerialization::countEntities(const json& entityJson)
    {
        size_t count = 1;
        if (entityJson.contains("children") && entityJson["children"].is_array())
        {
            for (const auto& child : entityJson["children"])
            {
                count += countEntities(child);
            }
        }
        return count;
    }

    bool SceneSerialization::loadSceneInto(std::string_view filename, scene::SceneGraphSystem& sceneGraph,
                                           SceneLoadProgressCallback progressCallback)
    {
        json sceneJson;

        // Phase 1: Validate file and parse JSON before modifying scene
        try
        {
            std::string filePath{filename};
            std::ifstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open file for reading: {}", filename);
                return false;
            }

            sceneJson = json::parse(file);
            file.close();

            if (!sceneJson.is_object())
            {
                vfLogError("Invalid scene file: root is not a JSON object");
                return false;
            }

            if (!sceneJson.contains("root") || !sceneJson["root"].is_object())
            {
                vfLogError("Invalid scene file: missing or invalid 'root' object");
                return false;
            }

        }
        catch (const json::parse_error& e)
        {
            vfLogError("JSON parse error while loading scene: {}", e.what());
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to read scene file: {}", e.what());
            return false;
        }

        // Phase 2: File validated - safe to clear and load
        try
        {
            size_t totalEntities = countEntities(sceneJson["root"]);
            size_t entitiesLoaded = 0;

            sceneGraph.clearScene();

            if (sceneJson.contains("physicsSettings") && sceneJson["physicsSettings"].is_object())
            {
                types::PhysicsSettings settings = types::PhysicsSettings::createDefault();
                deserializePhysicsSettings(sceneJson["physicsSettings"], settings);
                sceneGraph.setPhysicsSettings(settings);
            }
            else
            {
                sceneGraph.setPhysicsSettings(types::PhysicsSettings::createDefault());
            }

            if (sceneJson.contains("audioSettings") && sceneJson["audioSettings"].is_object())
            {
                types::AudioSettings audioSettings = types::AudioSettings::createDefault();
                deserializeAudioSettings(sceneJson["audioSettings"], audioSettings);
                sceneGraph.setAudioSettings(audioSettings);
            }
            else
            {
                sceneGraph.setAudioSettings(types::AudioSettings::createDefault());
            }

            if (sceneJson.contains("renderSettings") && sceneJson["renderSettings"].is_object())
            {
                types::RenderSettings renderSettings = types::RenderSettings::createDefault();
                deserializeRenderSettings(sceneJson["renderSettings"], renderSettings);
                sceneGraph.setRenderSettings(renderSettings);
            }
            else
            {
                sceneGraph.setRenderSettings(types::RenderSettings::createDefault());
            }

            scene::Entity& root = sceneGraph.GetRoot();
            deserializeEntity(sceneJson["root"], root, sceneGraph, true, progressCallback, entitiesLoaded,
                              totalEntities);

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to deserialize scene: {}", e.what());
            sceneGraph.clearScene();
            return false;
        }
    }

    bool SceneSerialization::saveScene(scene::SceneGraphSystem& sceneGraph, std::string_view filename)
    {
        try
        {
            json sceneJson;
            sceneJson["version"] = "1.1";

            scene::Entity& root = sceneGraph.GetRoot();

            sceneJson["root"] = serializeEntity(root);
            sceneJson["physicsSettings"] = serializePhysicsSettings(sceneGraph.getPhysicsSettings());
            sceneJson["audioSettings"] = serializeAudioSettings(sceneGraph.getAudioSettings());
            sceneJson["renderSettings"] = serializeRenderSettings(sceneGraph.getRenderSettings());

            std::string filePath{filename};
            std::ofstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open file for writing: {}", filename);
                return false;
            }

            file << sceneJson.dump(2);
            file.close();

            vfLogInfo("Scene saved successfully to: {}", filename);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save scene: {}", e.what());
            return false;
        }
    }

    json SceneSerialization::createSnapshot(scene::SceneGraphSystem& sceneGraph)
    {
        try
        {
            json snapshot;
            snapshot["version"] = "1.1";

            scene::Entity& root = sceneGraph.GetRoot();
            snapshot["root"] = serializeEntity(root);
            snapshot["physicsSettings"] = serializePhysicsSettings(sceneGraph.getPhysicsSettings());
            snapshot["audioSettings"] = serializeAudioSettings(sceneGraph.getAudioSettings());
            snapshot["renderSettings"] = serializeRenderSettings(sceneGraph.getRenderSettings());

            return snapshot;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to create scene snapshot: {}", e.what());
            return json();
        }
    }

    bool SceneSerialization::restoreFromSnapshot(const json& snapshot, scene::SceneGraphSystem& sceneGraph)
    {
        try
        {
            if (!snapshot.is_object())
            {
                vfLogError("Invalid snapshot: not a JSON object");
                return false;
            }

            if (!snapshot.contains("root") || !snapshot["root"].is_object())
            {
                vfLogError("Invalid snapshot: missing or invalid 'root' object");
                return false;
            }

            sceneGraph.clearScene();

            if (snapshot.contains("physicsSettings") && snapshot["physicsSettings"].is_object())
            {
                types::PhysicsSettings settings = types::PhysicsSettings::createDefault();
                deserializePhysicsSettings(snapshot["physicsSettings"], settings);
                sceneGraph.setPhysicsSettings(settings);
            }

            if (snapshot.contains("audioSettings") && snapshot["audioSettings"].is_object())
            {
                types::AudioSettings audioSettings = types::AudioSettings::createDefault();
                deserializeAudioSettings(snapshot["audioSettings"], audioSettings);
                sceneGraph.setAudioSettings(audioSettings);
            }

            if (snapshot.contains("renderSettings") && snapshot["renderSettings"].is_object())
            {
                types::RenderSettings renderSettings = types::RenderSettings::createDefault();
                deserializeRenderSettings(snapshot["renderSettings"], renderSettings);
                sceneGraph.setRenderSettings(renderSettings);
            }

            scene::Entity& root = sceneGraph.GetRoot();
            size_t entitiesLoaded = 0;
            size_t totalEntities = countEntities(snapshot["root"]);
            deserializeEntity(snapshot["root"], root, sceneGraph, true, nullptr, entitiesLoaded, totalEntities);

            vfLogInfo("Scene restored from snapshot successfully");
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to restore scene from snapshot: {}", e.what());
            sceneGraph.clearScene();
            return false;
        }
    }
}
