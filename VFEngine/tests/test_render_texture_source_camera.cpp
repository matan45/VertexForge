// CPU-only coverage for the render-texture source-camera reference (VK-1414).
//
// An RTT entity may render from a SEPARATE camera entity referenced by name
// (RenderTextureComponent::sourceCameraName), instead of requiring a co-located
// CameraComponent. Only the NAME is serialized; the runtime entt handle
// (sourceCamera) is resolved on load and must never be written to the scene.
//
// Coverage:
//   1. Scene round-trip: an RTT whose sourceCameraName points at a named camera
//      entity saves the name, reloads it, and resolves sourceCamera back to that
//      camera entity (resolveRenderTextureSourceNames runs inside loadSceneInto).
//   2. Default (no source set): no "sourceCameraName" key is written and the
//      reloaded component has an empty name + entt::null handle.
//   3. Service-level: setRenderTextureData with a sourceCameraName eager-resolves
//      to the matching CAMERA entity on the singleton registry without touching
//      textureId; an unmatched name resolves to entt::null.
//
// All CPU-only: no Vulkan device, no GLFW window. The serialization seam used is
// the public saveScene / loadSceneInto (the per-component serialize helpers are
// private), exactly as test_billboard_serialization.cpp drives RTT-source-name
// resolution for billboards. Tests must link ECSRegistry (per CLAUDE.md) so the
// registry singleton resolves to one instance.

#include <doctest.h>

#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <asset/AssetDatabase.hpp>
#include <nlohmann/json.hpp>

#include <impl/components/RenderTextureComponentService.hpp>
#include <data/EntityConversion.hpp>

#include <rendertexture/RenderTextureTypes.hpp>

#include <entt/entt.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path sourceCameraTestRoot()
    {
        return fs::temp_directory_path() / "vf_rtt_source_camera_tests";
    }

    void resetTestRoot()
    {
        std::error_code ec;
        fs::remove_all(sourceCameraTestRoot(), ec);
        fs::create_directories(sourceCameraTestRoot(), ec);
        asset::AssetDatabase::instance().clear();
    }

    json readJson(const fs::path& path)
    {
        std::ifstream file(path);
        REQUIRE(file.is_open());
        json j;
        file >> j;
        return j;
    }

    // Find the first entity that carries a NameComponent with the given name.
    entt::entity findEntityByName(const std::string& name)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::NameComponent>();
        for (auto entity : view)
        {
            if (view.get<components::NameComponent>(entity).name == name)
                return entity;
        }
        return entt::null;
    }
}

TEST_SUITE("RenderTextureSourceCamera")
{
    TEST_CASE("sourceCameraName round-trips and resolves to the camera entity on load")
    {
        resetTestRoot();

        const std::string camName = "VK1414_RoundTrip_SourceCam";
        const std::string rttName = "VK1414_RoundTrip_RTT";

        scene::SceneGraphSystem source;

        // A separate camera entity (the source) as a child of root.
        scene::Entity camEntity(camName);
        camEntity.addComponent<components::CameraComponent>();
        source.GetRoot().addChildren(camEntity);

        // The RTT entity references the camera by name (no own camera needed).
        scene::Entity rttEntity(rttName);
        auto& rtt = rttEntity.addComponent<components::RenderTextureComponent>();
        rtt.width = 640u;
        rtt.height = 360u;
        rtt.sourceCameraName = camName;
        source.GetRoot().addChildren(rttEntity);

        fs::path scenePath = sourceCameraTestRoot() / "SourceCameraRoundTrip.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        // The name is serialized into the RTT component block.
        auto sceneJson = readJson(scenePath);
        bool foundSerializedName = false;
        std::function<void(const json&)> scan = [&](const json& node)
        {
            if (node.contains("components") &&
                node["components"].contains("renderTexture") &&
                node["components"]["renderTexture"].contains("sourceCameraName"))
            {
                if (node["components"]["renderTexture"]["sourceCameraName"].get<std::string>() == camName)
                    foundSerializedName = true;
            }
            if (node.contains("children"))
            {
                for (const auto& child : node["children"])
                    scan(child);
            }
        };
        REQUIRE(sceneJson.contains("root"));
        scan(sceneJson["root"]);
        CHECK(foundSerializedName);

        // Reload into a fresh scene; resolveRenderTextureSourceNames runs inside load.
        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));

        entt::entity loadedCam = findEntityByName(camName);
        entt::entity loadedRtt = findEntityByName(rttName);
        // Compare outside the macro: doctest's expression decomposition is ambiguous
        // against entt's operator==/!=(entity, null_t).
        const bool loadedCamValid = loadedCam != entt::null;
        const bool loadedRttValid = loadedRtt != entt::null;
        REQUIRE(loadedCamValid);
        REQUIRE(loadedRttValid);

        auto& registry = scene::EntityRegistry::getRegistry();
        REQUIRE(registry.all_of<components::RenderTextureComponent>(loadedRtt));
        const auto& loadedComp = registry.get<components::RenderTextureComponent>(loadedRtt);

        // Name survives the round-trip and the handle resolves to the loaded camera.
        CHECK(loadedComp.sourceCameraName == camName);
        CHECK(loadedComp.sourceCamera == loadedCam);
    }

    TEST_CASE("no source camera -> no JSON key, empty name, null handle")
    {
        resetTestRoot();

        const std::string rttName = "VK1414_NoSource_RTT";

        scene::SceneGraphSystem source;
        scene::Entity rttEntity(rttName);
        auto& rtt = rttEntity.addComponent<components::RenderTextureComponent>();
        rtt.width = 320u;
        rtt.height = 240u;
        // sourceCameraName intentionally left empty.
        source.GetRoot().addChildren(rttEntity);

        fs::path scenePath = sourceCameraTestRoot() / "NoSourceCamera.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        // Default (empty) source name must NOT emit the key.
        auto sceneJson = readJson(scenePath);
        bool anyRenderTextureBlock = false;
        bool sawSourceKey = false;
        std::function<void(const json&)> scan = [&](const json& node)
        {
            if (node.contains("components") && node["components"].contains("renderTexture"))
            {
                anyRenderTextureBlock = true;
                if (node["components"]["renderTexture"].contains("sourceCameraName"))
                    sawSourceKey = true;
            }
            if (node.contains("children"))
            {
                for (const auto& child : node["children"])
                    scan(child);
            }
        };
        REQUIRE(sceneJson.contains("root"));
        scan(sceneJson["root"]);
        REQUIRE(anyRenderTextureBlock);
        CHECK_FALSE(sawSourceKey);

        // Reload: empty name, null handle.
        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));

        entt::entity loadedRtt = findEntityByName(rttName);
        const bool loadedRttValid = loadedRtt != entt::null;
        REQUIRE(loadedRttValid);
        auto& registry = scene::EntityRegistry::getRegistry();
        REQUIRE(registry.all_of<components::RenderTextureComponent>(loadedRtt));
        const auto& loadedComp = registry.get<components::RenderTextureComponent>(loadedRtt);
        CHECK(loadedComp.sourceCameraName.empty());
        const bool sourceCameraIsNull = loadedComp.sourceCamera == entt::null;
        CHECK(sourceCameraIsNull);
    }

    TEST_CASE("setRenderTextureData resolves a camera name without touching textureId")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        const std::string camName = "VK1414_Service_SourceCam";

        // A named camera entity on the singleton registry.
        auto camHandle = registry.create();
        registry.emplace<components::NameComponent>(camHandle, camName);
        registry.emplace<components::CameraComponent>(camHandle);
        registry.emplace<components::TransformComponent>(camHandle);

        // An RTT entity with a pre-existing (runtime-owned) textureId.
        scene::Entity rttEntity(scene::EntityRegistry::getRegistry().create());
        auto& comp = rttEntity.addComponent<components::RenderTextureComponent>();
        comp.textureId = 7777u;

        services::RenderTextureComponentService service(std::make_shared<scene::SceneGraphSystem>());

        SUBCASE("matching name resolves to the camera entity")
        {
            services::RenderTextureData data;
            data.width = 512u;
            data.height = 512u;
            data.sourceCameraName = camName;

            const bool ok = service.setRenderTextureData(
                services::internal::toHandle(rttEntity.getHandle()), data);
            REQUIRE(ok);

            const auto& after = rttEntity.getComponent<components::RenderTextureComponent>();
            CHECK(after.sourceCameraName == camName);
            CHECK(after.sourceCamera == camHandle);
            // textureId is runtime-owned and must be untouched by the inspector path.
            CHECK(after.textureId == 7777u);
        }

        SUBCASE("unmatched name resolves to null")
        {
            services::RenderTextureData data;
            data.width = 512u;
            data.height = 512u;
            data.sourceCameraName = "VK1414_Service_NoSuchCamera";

            const bool ok = service.setRenderTextureData(
                services::internal::toHandle(rttEntity.getHandle()), data);
            REQUIRE(ok);

            const auto& after = rttEntity.getComponent<components::RenderTextureComponent>();
            CHECK(after.sourceCameraName == "VK1414_Service_NoSuchCamera");
            const bool sourceCameraIsNull = after.sourceCamera == entt::null;
            CHECK(sourceCameraIsNull);
            CHECK(after.textureId == 7777u);
        }
    }
}
