#include <doctest.h>

// VK-1726: LoadGame restores each script's saved @Saveable state when its instance is created, BEFORE its onStart.
// It used to restore on SceneLoadingCompletedNotification, when the new scene's instances did not exist yet
// (ScriptingServiceImpl::updateScripts creates them later), so nothing was ever restored and the load still
// reported success. These CPU-only cases drive the real ScriptingServiceImpl and SaveService with a mock provider
// and publish the scene notifications ScenePersistenceService would.

#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>

#include <events/EventDispatcher.hpp>
#include <events/editor/EditorModeEvents.hpp>
#include <events/project/ProjectEvents.hpp>
#include <events/save/SaveEvents.hpp>
#include <events/scene/ScenePersistenceEvents.hpp>
#include <impl/save/SaveService.hpp>
#include <impl/scripting/ScriptingServiceImpl.hpp>
#include <providers/scripting/IScriptingProvider.hpp>

#include <any>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    struct Call
    {
        std::string method;
        uint64_t instanceId;
        std::string argument;
    };

    // Records loadScript / setInstanceState / playVFX (which runs onStart). The class name of a script is the
    // text after "test://" in its path.
    class RestoreMockProvider : public services::IScriptingProvider
    {
    public:
        std::vector<Call> calls;
        std::map<uint64_t, std::string> classNames;
        uint64_t nextInstanceId = 1726000;

        int indexOf(const std::string& method, uint64_t id) const
        {
            for (int i = 0; i < static_cast<int>(calls.size()); ++i)
                if (calls[i].method == method && calls[i].instanceId == id) return i;
            return -1;
        }
        const Call* find(const std::string& method, uint64_t id) const
        {
            const int i = indexOf(method, id);
            return i < 0 ? nullptr : &calls[static_cast<size_t>(i)];
        }

        std::optional<services::ScriptInstanceInfo> loadScript(const std::string& path, services::EntityHandle) override
        {
            services::ScriptInstanceInfo info;
            info.instanceId = nextInstanceId++;
            const std::string prefix = "test://";
            info.className = path.rfind(prefix, 0) == 0 ? path.substr(prefix.size()) : path;
            info.scriptPath = path;
            classNames[info.instanceId] = info.className;
            calls.push_back({"load", info.instanceId, path});
            return info;
        }
        bool setInstanceState(uint64_t id, const std::string& state) override
        {
            calls.push_back({"setState", id, state});
            return true;
        }
        void playVFX(uint64_t id) override { calls.push_back({"onStart", id, {}}); }
        std::string getInstanceClassName(uint64_t id) const override
        {
            const auto it = classNames.find(id);
            return it == classNames.end() ? std::string() : it->second;
        }

        // --- everything else: trivial stubs ---
        bool init() override { return true; }
        void cleanUp() override {}
        bool isInitialized() const override { return true; }
        services::ScriptBuildResult buildScripts(const std::string&) override { return {}; }
        void cleanScripts(const std::string&) override {}
        bool isCompiled() const override { return true; }
        bool loadCompiledScripts(const std::string&) override { return true; }
        void unloadScript(uint64_t) override {}
        void unloadAllScripts() override {}
        bool isScriptLoaded(uint64_t) const override { return true; }
        void callOnStart(uint64_t) override {}
        void callOnUpdate(uint64_t, float) override {}
        void callOnLateUpdate(uint64_t, float) override {}
        void callOnFixedUpdate(uint64_t, float) override {}
        void callOnEnable(uint64_t) override {}
        void callOnDisable(uint64_t) override {}
        void callOnDestroy(uint64_t) override {}
        void tickCoroutines(float) override {}
        void tickFixedUpdateCoroutines() override {}
        std::string callMethodWithReturn(uint64_t, const std::string&, const std::vector<std::any>&) override { return {}; }
        bool hasMethod(uint64_t, const std::string&) const override { return false; }
        void setInstancePriority(uint64_t, int) override {}
        std::optional<services::ScriptError> getLastError() const override { return std::nullopt; }
        void clearError() override {}
        void setScriptLibraryPath(const std::string&) override {}
        void startDebugServer(int) override {}
        void stopDebugServer() override {}
        bool isDebuggerActive() const override { return false; }
        void registerPluginNativeFunction(const std::string&, std::any) override {}
        void unregisterPluginNativeFunction(const std::string&) override {}
        std::string getInstanceState(uint64_t) override { return {}; }
        bool isSaveableInstance(uint64_t) const override { return false; }
        std::vector<uint64_t> getAllInstanceIds() const override { return {}; }
        services::EntityHandle getInstanceEntity(uint64_t) const override { return services::EntityHandle::invalid(); }
        std::string getInstanceScriptPath(uint64_t) const override { return {}; }
    };

    const std::wstring kHebrewWide = L"שמירה";

    struct SavedState
    {
        uint64_t entityUUID;
        std::string className;
        std::string state;
    };

    // The dispatcher (cleared at both ends), a slot on disk, the stubbed scene and project events, the services.
    struct RestoreFixture
    {
        struct DispatcherReset
        {
            DispatcherReset() { events::EventDispatcher::instance().clear(); }
            ~DispatcherReset() { events::EventDispatcher::instance().clear(); }
        } dispatcherReset;

        fs::path root;
        std::vector<std::string> loadedScenes;  // what LoadSceneCommand received, spelled as SaveService spells it
        std::vector<events::save::LoadGameCompletedNotification> completions;
        events::ScopedSubscription completionSubscription;

        RestoreMockProvider provider;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        std::unique_ptr<services::ScriptingServiceImpl> scripting;
        std::unique_ptr<services::SaveService> saves;
        std::vector<scene::Entity> entities;

        explicit RestoreFixture(const std::wstring& name)
        {
            root = fs::temp_directory_path() / (L"VertexForge_SaveRestore_" + name + L"_" + kHebrewWide);
            std::error_code ec;
            fs::remove_all(root, ec);

            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.registerCommandHandler<events::scene::LoadSceneCommand>(
                [this](const events::scene::LoadSceneCommand& cmd)
                {
                    loadedScenes.push_back(cmd.filePath);
                    return true;
                });
            // ScriptingServiceImpl::updateScripts asks for the project to resolve a relative script path.
            dispatcher.registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    return std::nullopt;
                });
            completionSubscription = events::ScopedSubscription(
                dispatcher.subscribe<events::save::LoadGameCompletedNotification>(
                    [this](const events::save::LoadGameCompletedNotification& n) { completions.push_back(n); }));

            scripting = std::make_unique<services::ScriptingServiceImpl>(&provider, sceneGraph);
            saves = std::make_unique<services::SaveService>(sceneGraph, &provider, root);
            saves->registerEventHandlers(dispatcher);
        }

        ~RestoreFixture()
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            for (scene::Entity& e : entities)
            {
                if (registry.valid(e.getHandle()))
                    registry.destroy(e.getHandle());
            }
            saves.reset();
            scripting.reset();
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        void writeSlot(const std::string& slot, const std::vector<SavedState>& states, const std::string* rawScripts = nullptr)
        {
            const fs::path slotDir = root / L"saves" / fs::path(slot);
            fs::create_directories(slotDir);
            std::ofstream(slotDir / L"scene.vfScene") << "{}";
            std::string text;
            if (rawScripts)
            {
                text = *rawScripts;
            }
            else
            {
                text = "{\"version\": 1, \"instances\": [";
                for (size_t i = 0; i < states.size(); ++i)
                {
                    if (i) text += ",";
                    text += "{\"entityUUID\": " + std::to_string(states[i].entityUUID) + ", \"className\": \"" +
                            states[i].className + "\", \"scriptPath\": \"test://" + states[i].className +
                            "\", \"state\": \"" + states[i].state + "\"}";
                }
                text += "]}";
            }
            std::ofstream(slotDir / L"scripts.json") << text;
        }

        // An entity with one not-yet-loaded script, as a freshly loaded scene has it.
        uint64_t addScriptedEntity(const std::string& name, uint64_t uuid, const std::string& className)
        {
            scene::Entity e(name);
            e.getComponent<components::UUIDComponent>() = components::UUIDComponent(uuid);
            auto& sc = e.addComponent<components::ScriptComponent>();
            components::ScriptEntry entry;
            entry.scriptPath = "test://" + className;
            entry.enabled = true;
            sc.scripts.push_back(entry);
            entities.push_back(e);
            return uuid;
        }

        uint64_t instanceOf(const std::string& className) const
        {
            for (const auto& [id, name] : provider.classNames)
                if (name == className) return id;
            return 0;
        }

        void publishCleared() { events::EventDispatcher::instance().publish(events::scene::SceneClearedNotification{}); }
        // ScenePersistenceService::finishLoad passes the path it was asked to load on unchanged.
        void publishCompleted(bool success)
        {
            REQUIRE_FALSE(loadedScenes.empty());
            events::scene::SceneLoadingCompletedNotification n;
            n.scenePath = loadedScenes.back();
            n.success = success;
            events::EventDispatcher::instance().publish(n);
        }
    };
}

TEST_SUITE("SaveRestore")
{
    TEST_CASE("a saved state is applied when its instance is created, before onStart")
    {
        RestoreFixture f(L"BeforeStart");
        f.writeSlot("slot1", {{17260001, "PlayerState", "{\\\"health\\\":42}"}});

        REQUIRE(f.saves->loadGame("slot1"));
        REQUIRE(f.loadedScenes.size() == 1);
        CHECK(fs::path(f.loadedScenes[0]).lexically_normal() ==
              (f.root / L"saves" / L"slot1" / L"scene.vfScene").lexically_normal());
        CHECK(f.completions.empty());

        // The editor's order: the scene loads whole, then the next script pass creates the instances.
        f.publishCleared();
        f.addScriptedEntity("Player", 17260001, "PlayerState");
        f.publishCompleted(true);
        CHECK(f.completions.empty());

        f.scripting->updateScripts(0.016f);

        const uint64_t id = f.instanceOf("PlayerState");
        REQUIRE(id != 0);
        const int setIndex = f.provider.indexOf("setState", id);
        const int startIndex = f.provider.indexOf("onStart", id);
        REQUIRE(setIndex >= 0);
        REQUIRE(startIndex >= 0);
        CHECK(setIndex < startIndex);
        CHECK(f.provider.find("setState", id)->argument == "{\"health\":42}");

        REQUIRE(f.completions.size() == 1);
        CHECK(f.completions[0].slotName == "slot1");
        CHECK(f.completions[0].success);
    }

    TEST_CASE("instances created while the scene still loads get their state too (the Runtime's incremental load)")
    {
        RestoreFixture f(L"Incremental");
        f.writeSlot("slot1", {{17260002, "Inventory", "{}"}});
        REQUIRE(f.saves->loadGame("slot1"));

        f.publishCleared();
        f.addScriptedEntity("Bag", 17260002, "Inventory");
        f.scripting->updateScripts(0.016f);  // a pass during the load: no completion yet
        CHECK(f.completions.empty());
        const uint64_t id = f.instanceOf("Inventory");
        REQUIRE(id != 0);
        CHECK(f.provider.indexOf("setState", id) >= 0);

        f.publishCompleted(true);
        f.scripting->updateScripts(0.016f);
        REQUIRE(f.completions.size() == 1);
        CHECK(f.completions[0].success);
    }

    TEST_CASE("an instance of the old scene, created before the slot's load cleared it, is left alone")
    {
        RestoreFixture f(L"OldScene");
        f.writeSlot("slot1", {{17260003, "Door", "{}"}});
        REQUIRE(f.saves->loadGame("slot1"));

        // Same UUID as the saved one, but the old scene's (the slot was saved from this scene).
        f.addScriptedEntity("OldDoor", 17260003, "Door");
        f.scripting->updateScripts(0.016f);
        const uint64_t oldId = f.instanceOf("Door");
        REQUIRE(oldId != 0);
        CHECK(f.provider.indexOf("setState", oldId) == -1);
    }

    TEST_CASE("nothing matched: the load reports a failure")
    {
        RestoreFixture f(L"Mismatch");
        f.writeSlot("slot1", {{17260004, "RenamedClass", "{}"}});
        REQUIRE(f.saves->loadGame("slot1"));

        f.publishCleared();
        f.addScriptedEntity("Player", 17260004, "PlayerState");
        f.publishCompleted(true);
        f.scripting->updateScripts(0.016f);

        REQUIRE(f.completions.size() == 1);
        CHECK_FALSE(f.completions[0].success);
        CHECK(f.completions[0].errorMessage.find("none of the 1 saved script states") != std::string::npos);
    }

    TEST_CASE("a slot without saved script states loads successfully")
    {
        RestoreFixture f(L"Empty");
        f.writeSlot("slot1", {});
        REQUIRE(f.saves->loadGame("slot1"));
        f.publishCleared();
        f.publishCompleted(true);
        f.scripting->updateScripts(0.016f);
        REQUIRE(f.completions.size() == 1);
        CHECK(f.completions[0].success);
    }

    TEST_CASE("a failed scene load, another load or the end of Play mode fails the pending load")
    {
        {
            RestoreFixture f(L"SceneFailed");
            f.writeSlot("slot1", {{1, "A", "{}"}});
            REQUIRE(f.saves->loadGame("slot1"));
            f.publishCleared();
            f.publishCompleted(false);
            REQUIRE(f.completions.size() == 1);
            CHECK_FALSE(f.completions[0].success);
        }
        {
            RestoreFixture f(L"Replaced");
            f.writeSlot("slot1", {{1, "A", "{}"}});
            REQUIRE(f.saves->loadGame("slot1"));
            events::scene::SceneLoadingStartedNotification other;
            other.scenePath = "Assets/scenes/Menu.vfScene";
            events::EventDispatcher::instance().publish(other);
            REQUIRE(f.completions.size() == 1);
            CHECK_FALSE(f.completions[0].success);
            CHECK(f.completions[0].errorMessage.find("Menu.vfScene") != std::string::npos);
        }
        {
            RestoreFixture f(L"PlayEnded");
            f.writeSlot("slot1", {{1, "A", "{}"}});
            REQUIRE(f.saves->loadGame("slot1"));
            events::editor::EditorModeChangedNotification stop;
            stop.previousMode = services::EditorMode::Play;
            stop.currentMode = services::EditorMode::Edit;
            events::EventDispatcher::instance().publish(stop);
            REQUIRE(f.completions.size() == 1);
            CHECK_FALSE(f.completions[0].success);

            // The Stop's snapshot restore that follows runs no stray restore.
            f.publishCleared();
            f.addScriptedEntity("A", 1, "A");
            f.scripting->updateScripts(0.016f);
            CHECK(f.provider.indexOf("setState", f.instanceOf("A")) == -1);
            CHECK(f.completions.size() == 1);
        }
    }

    TEST_CASE("leftover states go to matching scripts that start later in the scene")
    {
        RestoreFixture f(L"Leftovers");
        f.writeSlot("slot1", {{17260005, "Player", "{\\\"x\\\":1}"}, {17260006, "Chest", "{\\\"open\\\":true}"}});
        REQUIRE(f.saves->loadGame("slot1"));

        f.publishCleared();
        f.addScriptedEntity("Player", 17260005, "Player");
        f.publishCompleted(true);
        f.scripting->updateScripts(0.016f);
        REQUIRE(f.completions.size() == 1);
        CHECK(f.completions[0].success);

        // The chest's entity is spawned later (or activated): its state still arrives, before its onStart.
        f.addScriptedEntity("Chest", 17260006, "Chest");
        f.scripting->updateScripts(0.016f);
        const uint64_t chest = f.instanceOf("Chest");
        REQUIRE(chest != 0);
        REQUIRE(f.provider.find("setState", chest) != nullptr);
        CHECK(f.provider.find("setState", chest)->argument == "{\"open\":true}");
        CHECK(f.provider.indexOf("setState", chest) < f.provider.indexOf("onStart", chest));
        CHECK(f.completions.size() == 1);
    }

    TEST_CASE("an unreadable scripts.json refuses the load and keeps the current scene")
    {
        RestoreFixture f(L"Corrupt");
        const std::string broken = "{ not json";
        f.writeSlot("slot1", {}, &broken);
        CHECK_FALSE(f.saves->loadGame("slot1"));
        CHECK(f.loadedScenes.empty());
        CHECK(f.completions.empty());
    }
}
