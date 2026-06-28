#include <doctest.h>

// VK-1448: formalize the OPTIONAL onLateUpdate script lifecycle hook. The runtime plumbing
// already exists (ScriptingServiceImpl::lateUpdateScripts -> IScriptingProvider::callOnLateUpdate,
// scheduled after the Navmesh task in Runtime + Editor Play). These CPU-only tests lock the
// service-level FIRING CONTRACT of onLateUpdate against regression, with a mock provider in place
// of the real mType interpreter. True frame-graph ordering (onUpdate -> Navmesh -> onLateUpdate)
// is enforced by the task graph and asserted by code review, not here.

#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>

#include <impl/scripting/ScriptingServiceImpl.hpp>
#include <providers/scripting/IScriptingProvider.hpp>   // also pulls data/ScriptTypes + EntityHandle

#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <any>
#include <cstdint>

namespace
{
    struct Recorded
    {
        std::string method;
        uint64_t instanceId;
        float dt;
    };

    // Records callOnUpdate / callOnLateUpdate; every other IScriptingProvider method is a trivial
    // stub returning a default so the mock is concrete. Keyed assertions use unique instanceIds
    // because the global EntityRegistry (and therefore lateUpdateScripts' iteration) is shared
    // across the whole Tests process.
    class MockScriptingProvider : public services::IScriptingProvider
    {
    public:
        std::vector<Recorded> calls;

        int countLate(uint64_t id) const
        {
            int n = 0;
            for (const auto& c : calls)
                if (c.method == "onLateUpdate" && c.instanceId == id) ++n;
            return n;
        }
        const Recorded* firstLate(uint64_t id) const
        {
            for (const auto& c : calls)
                if (c.method == "onLateUpdate" && c.instanceId == id) return &c;
            return nullptr;
        }
        int indexOf(const std::string& method, uint64_t id) const
        {
            for (int i = 0; i < static_cast<int>(calls.size()); ++i)
                if (calls[i].method == method && calls[i].instanceId == id) return i;
            return -1;
        }

        // --- recorded ---
        void callOnLateUpdate(uint64_t id, float dt) override { calls.push_back({"onLateUpdate", id, dt}); }
        void callOnUpdate(uint64_t id, float dt) override { calls.push_back({"onUpdate", id, dt}); }

        // --- VM lifecycle ---
        bool init() override { return true; }
        void cleanUp() override {}
        bool isInitialized() const override { return true; }

        // --- build ---
        services::ScriptBuildResult buildScripts(const std::string&) override { return {}; }
        void cleanScripts(const std::string&) override {}
        bool isCompiled() const override { return true; }
        bool loadCompiledScripts(const std::string&) override { return true; }

        // --- loading ---
        std::optional<services::ScriptInstanceInfo> loadScript(const std::string&, services::EntityHandle) override { return std::nullopt; }
        void unloadScript(uint64_t) override {}
        void unloadAllScripts() override {}
        bool isScriptLoaded(uint64_t) const override { return true; }

        // --- other lifecycle ---
        void callOnStart(uint64_t) override {}
        void callOnFixedUpdate(uint64_t, float) override {}
        void callOnEnable(uint64_t) override {}
        void callOnDisable(uint64_t) override {}
        void callOnDestroy(uint64_t) override {}

        // --- coroutines ---
        void tickCoroutines(float) override {}
        void tickFixedUpdateCoroutines() override {}

        // --- misc ---
        std::string callMethodWithReturn(uint64_t, const std::string&, const std::vector<std::any>&) override { return {}; }
        bool hasMethod(uint64_t, const std::string&) const override { return false; }
        void playVFX(uint64_t) override {}
        void setInstancePriority(uint64_t, int) override {}

        // --- error handling ---
        std::optional<services::ScriptError> getLastError() const override { return std::nullopt; }
        void clearError() override {}

        // --- library / debugger ---
        void setScriptLibraryPath(const std::string&) override {}
        void startDebugServer(int) override {}
        void stopDebugServer() override {}
        bool isDebuggerActive() const override { return false; }

        // --- plugin natives ---
        void registerPluginNativeFunction(const std::string&, std::any) override {}
        void unregisterPluginNativeFunction(const std::string&) override {}

        // --- save / load ---
        std::string getInstanceState(uint64_t) override { return {}; }
        bool setInstanceState(uint64_t, const std::string&) override { return true; }
        bool isSaveableInstance(uint64_t) const override { return false; }
        std::vector<uint64_t> getAllInstanceIds() const override { return {}; }
        services::EntityHandle getInstanceEntity(uint64_t) const override { return services::EntityHandle::invalid(); }
        std::string getInstanceClassName(uint64_t) const override { return {}; }
        std::string getInstanceScriptPath(uint64_t) const override { return {}; }
    };

    // Builds an entity owning one ScriptComponent with a single ScriptEntry. instanceId != 0 marks
    // the script "loaded"; started/enabled drive the lateUpdateScripts gate.
    scene::Entity makeScriptedEntity(const std::string& name, uint64_t instanceId, bool enabled, bool started)
    {
        scene::Entity e(name);  // adds NameComponent(isActive=true) + UUID + Transform
        auto& sc = e.addComponent<components::ScriptComponent>();
        components::ScriptEntry entry;
        entry.scriptPath = "test://" + name;
        entry.enabled = enabled;
        entry.started = started;
        entry.instanceId = instanceId;
        sc.scripts.push_back(entry);
        return e;
    }

    void destroyEntity(scene::Entity& e)
    {
        scene::EntityRegistry::getRegistry().destroy(e.getHandle());
    }
}

TEST_SUITE("ScriptingLifecycle")
{
    TEST_CASE("onLateUpdate fires once with the forwarded deltaTime for an enabled+started script")
    {
        MockScriptingProvider provider;
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::ScriptingServiceImpl service(&provider, sceneGraph);

        const uint64_t ID = 1448001;
        auto e = makeScriptedEntity("LateUpdateActive", ID, /*enabled*/ true, /*started*/ true);

        service.lateUpdateScripts(0.016f);

        CHECK(provider.countLate(ID) == 1);
        REQUIRE(provider.firstLate(ID) != nullptr);
        CHECK(provider.firstLate(ID)->dt == doctest::Approx(0.016f));

        destroyEntity(e);
    }

    TEST_CASE("onLateUpdate does not fire when the script entry is disabled")
    {
        MockScriptingProvider provider;
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::ScriptingServiceImpl service(&provider, sceneGraph);

        const uint64_t ID = 1448002;
        auto e = makeScriptedEntity("LateUpdateDisabled", ID, /*enabled*/ false, /*started*/ true);

        service.lateUpdateScripts(0.016f);

        CHECK(provider.countLate(ID) == 0);
        destroyEntity(e);
    }

    TEST_CASE("onLateUpdate does not fire before the script has started")
    {
        MockScriptingProvider provider;
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::ScriptingServiceImpl service(&provider, sceneGraph);

        const uint64_t ID = 1448003;
        auto e = makeScriptedEntity("LateUpdateNotStarted", ID, /*enabled*/ true, /*started*/ false);

        service.lateUpdateScripts(0.016f);

        CHECK(provider.countLate(ID) == 0);
        destroyEntity(e);
    }

    TEST_CASE("onLateUpdate does not fire when the script has no loaded instance (instanceId == 0)")
    {
        MockScriptingProvider provider;
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::ScriptingServiceImpl service(&provider, sceneGraph);

        auto e = makeScriptedEntity("LateUpdateUnloaded", /*instanceId*/ 0, /*enabled*/ true, /*started*/ true);

        service.lateUpdateScripts(0.016f);

        // No onLateUpdate may be recorded for the unloaded (instanceId 0) entry.
        CHECK(provider.indexOf("onLateUpdate", 0) == -1);
        destroyEntity(e);
    }

    TEST_CASE("onLateUpdate does not fire for an inactive entity")
    {
        MockScriptingProvider provider;
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::ScriptingServiceImpl service(&provider, sceneGraph);

        const uint64_t ID = 1448004;
        auto e = makeScriptedEntity("LateUpdateInactive", ID, /*enabled*/ true, /*started*/ true);
        e.getComponent<components::NameComponent>().isActive = false;

        service.lateUpdateScripts(0.016f);

        CHECK(provider.countLate(ID) == 0);
        destroyEntity(e);
    }

    TEST_CASE("onLateUpdate fires once per started entry on the same entity")
    {
        MockScriptingProvider provider;
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::ScriptingServiceImpl service(&provider, sceneGraph);

        const uint64_t ID_A = 1448005;
        const uint64_t ID_B = 1448006;

        scene::Entity e("LateUpdateTwoScripts");
        auto& sc = e.addComponent<components::ScriptComponent>();
        components::ScriptEntry a;
        a.scriptPath = "test://A"; a.enabled = true; a.started = true; a.instanceId = ID_A; a.inputPriority = 10;
        components::ScriptEntry b;
        b.scriptPath = "test://B"; b.enabled = true; b.started = true; b.instanceId = ID_B; b.inputPriority = 0;
        sc.scripts.push_back(a);
        sc.scripts.push_back(b);

        service.lateUpdateScripts(0.033f);

        CHECK(provider.countLate(ID_A) == 1);
        CHECK(provider.countLate(ID_B) == 1);

        destroyEntity(e);
    }

    TEST_CASE("onUpdate precedes onLateUpdate when both run within a frame")
    {
        MockScriptingProvider provider;
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::ScriptingServiceImpl service(&provider, sceneGraph);

        const uint64_t ID = 1448010;
        // started=true + instanceId!=0 means updateScripts skips the load/start branches and goes
        // straight to callOnUpdate (no real interpreter needed).
        auto e = makeScriptedEntity("UpdateThenLate", ID, /*enabled*/ true, /*started*/ true);

        service.updateScripts(0.02f);       // ClearConsumedActionsCommand is try/catch-guarded with no handlers
        service.lateUpdateScripts(0.02f);

        const int updIdx = provider.indexOf("onUpdate", ID);
        const int lateIdx = provider.indexOf("onLateUpdate", ID);
        REQUIRE(updIdx >= 0);
        REQUIRE(lateIdx >= 0);
        CHECK(updIdx < lateIdx);

        destroyEntity(e);
    }
}
