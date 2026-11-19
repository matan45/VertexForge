#include <doctest.h>

#include "test_scene_stubs.hpp"

#include "events/EventDispatcher.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "impl/project/ProjectServiceImpl.hpp"
#include "impl/scene/ScenePersistenceService.hpp"
#include "serialization/ProjectSerialization.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "components/Components.hpp"
#include <asset/AssetDatabase.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>

// ============================================================
// VK-1687: QueueStartupSceneCommand (ScenePersistenceService). A missing startup scene that is a
// .vfScene inside the project folder is built with newScene() plus a "Sun" directional light and a
// primary "Main Camera", saved in the current format (settings in the .vfSettings sidecar) with a
// fresh root uuid, then queued like any other scene. Nothing is ever created outside the project.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    std::string readText(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // A project "<temp>/<name>/Game/Game.vfproj" with the given working directory and startup scene,
    // loaded into a ProjectServiceImpl, plus a ScenePersistenceService on its own scene graph.
    struct StarterSceneFixture
    {
        fs::path root;
        fs::path project;
        services::ProjectServiceImpl projectService;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        std::unique_ptr<services::ScenePersistenceService> persistence;

        StarterSceneFixture(const char* name, const std::string& startupScene,
                            const std::string& workingDirectory = ".")
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.clear();
            scene::EntityRegistry::init();
            asset::AssetDatabase::instance().clear();

            root = fs::temp_directory_path() / name;
            std::error_code ec;
            fs::remove_all(root, ec);
            project = root / "Game";
            fs::create_directories(project);

            config::ProjectConfig config;
            config.projectName = "StarterSceneTest";
            config.version = "1.0";
            config.workingDirectory = workingDirectory;
            config.startupScene = startupScene;
            REQUIRE(serialization::ProjectSerialization::saveProject(config, (project / "Game.vfproj").string()));

            test_scene_stubs::stubSceneLoadCommands();
            projectService.registerEventHandlers();
            REQUIRE(projectService.loadProject((project / "Game.vfproj").string()));

            sceneGraph = std::make_shared<scene::SceneGraphSystem>();
            persistence = std::make_unique<services::ScenePersistenceService>(sceneGraph, nullptr);
            persistence->registerEventHandlers(dispatcher);
        }

        ~StarterSceneFixture()
        {
            sceneGraph->clearScene();
            auto& registry = scene::EntityRegistry::getRegistry();
            const entt::entity sceneRoot = sceneGraph->GetRoot().getHandle();
            if (registry.valid(sceneRoot))
            {
                registry.destroy(sceneRoot);
            }
            persistence.reset();
            events::EventDispatcher::instance().clear();
            asset::AssetDatabase::instance().clear();
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        StarterSceneFixture(const StarterSceneFixture&) = delete;
        StarterSceneFixture& operator=(const StarterSceneFixture&) = delete;

        events::scene::StartupSceneResult queueStartupScene()
        {
            return events::EventDispatcher::instance().execute(events::scene::QueueStartupSceneCommand{});
        }

        uint64_t rootUuid()
        {
            return sceneGraph->GetRoot().getUUID().getValue();
        }
    };
}

TEST_SUITE("StarterScene")
{
    TEST_CASE("a missing startup scene inside the project is created in the current format and loaded")
    {
        StarterSceneFixture f("VertexForge_StarterScene_Created", "scenes/Main.vfScene");
        const uint64_t rootUuidBefore = f.rootUuid();
        const fs::path scenePath = f.project / "scenes" / "Main.vfScene";
        REQUIRE_FALSE(fs::exists(scenePath));

        const events::scene::StartupSceneResult result = f.queueStartupScene();
        CHECK(result.created);
        CHECK(result.queued);
        CHECK(result.warning.empty());
        std::error_code ec;
        CHECK(fs::equivalent(fs::path(result.scenePath), scenePath, ec));

        // The current format: settings live in the sidecar the scene references.
        REQUIRE(fs::exists(scenePath));
        CHECK(fs::exists(f.project / "scenes" / "Main.vfSettings"));
        const nlohmann::json sceneJson = nlohmann::json::parse(readText(scenePath));
        CHECK(sceneJson.at("version") == "1.0");
        CHECK(sceneJson.at("settingsRef").is_string());
        CHECK(sceneJson.at("settingsRefPath") == "Main.vfSettings");
        CHECK_FALSE(sceneJson.contains("renderSettings"));
        CHECK_FALSE(sceneJson.contains("physicsSettings"));
        CHECK_FALSE(sceneJson.contains("audioSettings"));

        const nlohmann::json& rootJson = sceneJson.at("root");
        REQUIRE(rootJson.at("uuid").is_number_unsigned());
        const uint64_t savedRootUuid = rootJson.at("uuid").get<uint64_t>();
        CHECK(savedRootUuid != rootUuidBefore);
        CHECK(savedRootUuid != 12897697530608323519ull); // the launcher template's shared root uuid

        const nlohmann::json& children = rootJson.at("children");
        REQUIRE(children.is_array());
        CHECK(children.size() == 2);
        int lights = 0;
        int cameras = 0;
        for (const nlohmann::json& child : children)
        {
            const nlohmann::json& components = child.at("components");
            if (components.contains("directionalLight"))
            {
                ++lights;
                CHECK(child.at("name") == "Sun");
            }
            if (components.contains("camera"))
            {
                ++cameras;
                CHECK(child.at("name") == "Main Camera");
                CHECK(components.at("camera").at("isPrimary") == true);
            }
        }
        CHECK(lights == 1);
        CHECK(cameras == 1);

        // The queued load reads the file back like any other scene.
        f.persistence->update();
        CHECK(f.rootUuid() == savedRootUuid);
        int loadedLights = 0;
        int loadedCameras = 0;
        for (scene::Entity& child : f.sceneGraph->GetRoot().getChildren())
        {
            if (child.hasComponent<components::DirectionalLightComponent>())
            {
                ++loadedLights;
            }
            if (child.hasComponent<components::CameraComponent>())
            {
                ++loadedCameras;
                CHECK(child.getComponent<components::CameraComponent>().isPrimary);
            }
        }
        CHECK(loadedLights == 1);
        CHECK(loadedCameras == 1);

        // Once it exists, the scene is only queued.
        const events::scene::StartupSceneResult again = f.queueStartupScene();
        CHECK_FALSE(again.created);
        CHECK(again.queued);
        CHECK(again.warning.empty());
        f.persistence->update();
        CHECK(f.rootUuid() == savedRootUuid);
    }

    TEST_CASE("a startup scene outside the project folder is never created")
    {
        StarterSceneFixture f("VertexForge_StarterScene_Outside", "../Other/Main.vfScene");
        const events::scene::StartupSceneResult result = f.queueStartupScene();
        CHECK_FALSE(result.created);
        CHECK_FALSE(result.queued);
        CHECK_FALSE(result.warning.empty());
        CHECK_FALSE(fs::exists(f.root / "Other" / "Main.vfScene"));
        CHECK_FALSE(fs::exists(f.root / "Other"));
    }

    TEST_CASE("a moved project's stale absolute working directory never gets a scene")
    {
        // The working directory still names the folder the project was moved away from.
        const fs::path oldFolder = fs::temp_directory_path() / "VertexForge_StarterScene_OldFolder";
        std::error_code ec;
        fs::remove_all(oldFolder, ec);
        {
            StarterSceneFixture f("VertexForge_StarterScene_Stale", "scenes/Main.vfScene", oldFolder.string());
            const events::scene::StartupSceneResult result = f.queueStartupScene();
            CHECK_FALSE(result.created);
            CHECK_FALSE(result.queued);
            CHECK_FALSE(result.warning.empty());
        }
        CHECK_FALSE(fs::exists(oldFolder));
    }

    TEST_CASE("the editor's state folder and other file types never get a starter scene")
    {
        {
            StarterSceneFixture f("VertexForge_StarterScene_State", ".vertexforge/Main.vfScene");
            const events::scene::StartupSceneResult result = f.queueStartupScene();
            CHECK_FALSE(result.created);
            CHECK_FALSE(result.queued);
            CHECK_FALSE(fs::exists(f.project / ".vertexforge" / "Main.vfScene"));
        }
        {
            StarterSceneFixture f("VertexForge_StarterScene_Type", "scenes/Main.json");
            const events::scene::StartupSceneResult result = f.queueStartupScene();
            CHECK_FALSE(result.created);
            CHECK_FALSE(result.queued);
            CHECK_FALSE(fs::exists(f.project / "scenes" / "Main.json"));
        }
    }

    TEST_CASE("a settings file left beside a deleted startup scene is kept and blocks the starter scene")
    {
        StarterSceneFixture f("VertexForge_StarterScene_Sidecar", "scenes/Main.vfScene");
        const fs::path settingsPath = f.project / "scenes" / "Main.vfSettings";
        fs::create_directories(settingsPath.parent_path());
        const std::string tuned = "{\"version\": \"1.0\", \"renderSettings\": {\"tuned\": true}}";
        {
            std::ofstream settings(settingsPath, std::ios::binary);
            settings << tuned;
        }

        const events::scene::StartupSceneResult result = f.queueStartupScene();
        CHECK_FALSE(result.created);
        CHECK_FALSE(result.queued);
        CHECK(result.warning.find("Main.vfSettings") != std::string::npos);
        CHECK_FALSE(fs::exists(f.project / "scenes" / "Main.vfScene"));
        CHECK(readText(settingsPath) == tuned);
    }

    TEST_CASE("newScene gives the root a fresh uuid")
    {
        StarterSceneFixture f("VertexForge_StarterScene_NewScene", "scenes/Main.vfScene");
        const uint64_t before = f.rootUuid();
        REQUIRE(f.persistence->newScene());
        const uint64_t after = f.rootUuid();
        CHECK(after != before);
        CHECK(after != 0);
        CHECK(scene::EntityRegistry::findByUUID(after) == f.sceneGraph->GetRoot().getHandle());
        CHECK(scene::EntityRegistry::findByUUID(before) == entt::null);
    }
}
