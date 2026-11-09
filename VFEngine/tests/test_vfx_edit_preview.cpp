#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include <impl/vfx/VFXEditPreviewHandler.hpp>
#include <events/EventDispatcher.hpp>
#include <events/editor/EditorModeEvents.hpp>
#include <events/project/ResourceEvents.hpp>
#include <events/scene/EntityTransformEvents.hpp>
#include <events/scene/ScenePersistenceEvents.hpp>
#include <events/vfx/VFXEditPreviewEvents.hpp>
#include <events/vfx/VFXRuntimeEvents.hpp>
#include <data/EditorMode.hpp>
#include <data/EntityConversion.hpp>
#include <data/EntityHandle.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetRef.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

// VK-1657 — VFXEditPreviewHandler (vfx_play / vfx_stop): an edit-mode preview is a persistent runtime
// instance created from the VFX component (create -> apply overrides -> play). It follows the
// component and the transform (a changed value is applied in place; a removed override, a new effect
// or a loop change recreates), and ends on Edit->Play (immediately), scene clear, entity deletion or
// component removal (next update). A save of the effect recreates it one update later. The vfxruntime
// commands are mocked through the dispatcher, like test_vfx_prefab_playmode.cpp.

namespace
{
    using EditorMode = services::EditorMode;
    namespace rt = services::events::vfxruntime;

    struct RuntimeLog
    {
        struct Created
        {
            services::VFXInstanceId id = 0;
            services::VFXRuntimeParams params;
        };

        std::vector<std::string> calls; // "create:<id>", "apply:<id>", "play:<id>", ...
        std::vector<Created> created;
        std::vector<std::pair<services::VFXInstanceId, services::VFXEmitterOverrides>> applied;
        std::vector<services::VFXInstanceId> destroyed;
        std::vector<std::pair<services::VFXInstanceId, glm::mat4>> moved;
        services::VFXInstanceId nextId = 900;
        bool playMode = false;

        size_t count(const std::string& prefix) const
        {
            return static_cast<size_t>(std::count_if(calls.begin(), calls.end(),
                [&prefix](const std::string& call) { return call.rfind(prefix, 0) == 0; }));
        }
    };

    // Registers the mocks for one test and removes them again.
    class RuntimeMocks
    {
    public:
        explicit RuntimeMocks(RuntimeLog& log)
        {
            auto& d = ::events::EventDispatcher::instance();
            RuntimeLog* l = &log;
            d.registerCommandHandler<rt::CreateVFXInstanceCommand>(
                [l](const rt::CreateVFXInstanceCommand& c) -> services::VFXInstanceId
                {
                    const services::VFXInstanceId id = l->nextId++;
                    l->created.push_back({id, c.params});
                    l->calls.push_back("create:" + std::to_string(id));
                    return id;
                });
            d.registerCommandHandler<rt::ApplyVFXInstanceOverridesCommand>(
                [l](const rt::ApplyVFXInstanceOverridesCommand& c)
                {
                    l->applied.emplace_back(c.instanceId, c.overrides);
                    l->calls.push_back("apply:" + std::to_string(c.instanceId));
                });
            d.registerCommandHandler<rt::PlayVFXInstanceCommand>(
                [l](const rt::PlayVFXInstanceCommand& c) { l->calls.push_back("play:" + std::to_string(c.instanceId)); });
            d.registerCommandHandler<rt::ResetVFXInstanceCommand>(
                [l](const rt::ResetVFXInstanceCommand& c) { l->calls.push_back("reset:" + std::to_string(c.instanceId)); });
            d.registerCommandHandler<rt::DestroyVFXInstanceCommand>(
                [l](const rt::DestroyVFXInstanceCommand& c)
                {
                    l->destroyed.push_back(c.instanceId);
                    l->calls.push_back("destroy:" + std::to_string(c.instanceId));
                });
            d.registerCommandHandler<rt::SetVFXInstanceTransformCommand>(
                [l](const rt::SetVFXInstanceTransformCommand& c)
                {
                    l->moved.emplace_back(c.instanceId, c.worldTransform);
                    l->calls.push_back("move:" + std::to_string(c.instanceId));
                });
            d.registerQueryHandler<::events::editor::IsPlayModeQuery>(
                [l](const ::events::editor::IsPlayModeQuery&) { return l->playMode; });
        }

        ~RuntimeMocks()
        {
            auto& d = ::events::EventDispatcher::instance();
            d.unregisterCommandHandler<rt::CreateVFXInstanceCommand>();
            d.unregisterCommandHandler<rt::ApplyVFXInstanceOverridesCommand>();
            d.unregisterCommandHandler<rt::PlayVFXInstanceCommand>();
            d.unregisterCommandHandler<rt::ResetVFXInstanceCommand>();
            d.unregisterCommandHandler<rt::DestroyVFXInstanceCommand>();
            d.unregisterCommandHandler<rt::SetVFXInstanceTransformCommand>();
            d.unregisterQueryHandler<::events::editor::IsPlayModeQuery>();
        }

        RuntimeMocks(const RuntimeMocks&) = delete;
        RuntimeMocks& operator=(const RuntimeMocks&) = delete;
    };

    // An entity with a VFX component on a registered effect and a world transform.
    struct PreviewEntity
    {
        scene::Entity entity;
        services::EntityHandle handle;

        explicit PreviewEntity(const char* name, const char* effectPath = "tests/preview_fire.vfvfx")
            : entity(name)
        {
            auto& vfxComp = entity.addComponent<components::VFXComponent>();
            vfxComp.vfxRef = asset::AssetRef::fromGUID(
                asset::AssetDatabase::instance().registerAsset(effectPath, resource::AssetType::VFX));
            entity.addComponent<components::WorldTransformComponent>();
            handle = services::internal::toHandle(entity.getHandle());
        }

        ~PreviewEntity()
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.valid(entity.getHandle()))
                registry.destroy(entity.getHandle());
        }

        components::VFXComponent& vfx() { return entity.getComponent<components::VFXComponent>(); }
    };

    void publishMode(EditorMode previous, EditorMode current)
    {
        ::events::editor::EditorModeChangedNotification n;
        n.previousMode = previous;
        n.currentMode = current;
        ::events::EventDispatcher::instance().publish(n);
    }
}

TEST_SUITE("VFXEditPreview")
{
    TEST_CASE("start creates a persistent instance, applies the overrides, then plays")
    {
        asset::AssetDatabase::instance().clear();
        RuntimeLog log;
        RuntimeMocks mocks(log);
        services::VFXEditPreviewHandler handler;

        PreviewEntity torch("PreviewTorch");
        torch.vfx().loop = true;
        torch.vfx().overrides = {{"startSize", 1.6f}, {"colorTint", glm::vec4(1.0f, 0.6f, 0.25f, 1.0f)}};

        const services::VFXEditPreviewResult result = handler.start(torch.handle);
        REQUIRE(result.started);
        CHECK(result.error.empty());

        REQUIRE(log.created.size() == 1);
        const services::VFXInstanceId id = log.created[0].id;
        CHECK(result.status.instanceId == id);
        CHECK(result.status.entity == torch.handle);
        CHECK(log.created[0].params.persistent);
        CHECK(log.created[0].params.loop);
        CHECK(log.created[0].params.vfxAssetPath == torch.vfx().vfxRef.resolve());

        const std::vector<std::string> expected{
            "create:" + std::to_string(id), "apply:" + std::to_string(id), "play:" + std::to_string(id)};
        CHECK(log.calls == expected);
        REQUIRE(log.applied.size() == 1);
        REQUIRE(log.applied[0].second.startSize.has_value());
        CHECK(*log.applied[0].second.startSize == doctest::Approx(1.6f));
        REQUIRE(log.applied[0].second.colorTint.has_value());
        CHECK(log.applied[0].second.colorTint->y == doctest::Approx(0.6f));

        // The component's Play-mode fields stay untouched.
        CHECK(torch.vfx().runtimeInstanceId == 0u);
        CHECK_FALSE(torch.vfx().isPlaying);

        const auto previews = handler.list(services::EntityHandle::invalid());
        REQUIRE(previews.size() == 1);
        CHECK(previews[0].instanceId == id);
    }

    TEST_CASE("no overrides: create then play, nothing applied")
    {
        asset::AssetDatabase::instance().clear();
        RuntimeLog log;
        RuntimeMocks mocks(log);
        services::VFXEditPreviewHandler handler;
        PreviewEntity torch("PreviewNoOverrides");

        REQUIRE(handler.start(torch.handle).started);
        CHECK(log.count("create:") == 1);
        CHECK(log.count("apply:") == 0);
        CHECK(log.count("play:") == 1);
    }

    TEST_CASE("starting a running preview restarts it in place")
    {
        asset::AssetDatabase::instance().clear();
        RuntimeLog log;
        RuntimeMocks mocks(log);
        services::VFXEditPreviewHandler handler;
        PreviewEntity torch("PreviewRestart");

        REQUIRE(handler.start(torch.handle).started);
        const services::VFXInstanceId id = log.created[0].id;
        REQUIRE(handler.start(torch.handle).started);

        CHECK(log.created.size() == 1);
        CHECK(log.destroyed.empty());
        CHECK(log.count("reset:" + std::to_string(id)) == 1);
    }

    TEST_CASE("component and transform edits follow the preview")
    {
        asset::AssetDatabase::instance().clear();
        RuntimeLog log;
        RuntimeMocks mocks(log);
        services::VFXEditPreviewHandler handler;
        PreviewEntity torch("PreviewFollow");
        torch.vfx().overrides = {{"startSize", 1.6f}};

        REQUIRE(handler.start(torch.handle).started);
        const services::VFXInstanceId first = log.created[0].id;

        SUBCASE("a changed value is applied in place")
        {
            torch.vfx().overrides = {{"startSize", 2.0f}};
            handler.update(0.016f);
            CHECK(log.created.size() == 1);
            CHECK(log.destroyed.empty());
            REQUIRE(log.applied.size() == 2);
            CHECK(log.applied[1].first == first);
            REQUIRE(log.applied[1].second.startSize.has_value());
            CHECK(*log.applied[1].second.startSize == doctest::Approx(2.0f));

            handler.update(0.016f); // unchanged now: nothing more is applied
            CHECK(log.applied.size() == 2);
        }

        SUBCASE("a removed override recreates the instance")
        {
            torch.vfx().overrides.clear();
            handler.update(0.016f);
            CHECK(log.destroyed == std::vector<services::VFXInstanceId>{first});
            REQUIRE(log.created.size() == 2);
            CHECK(log.created[1].params.persistent);
        }

        SUBCASE("a different effect recreates the instance")
        {
            torch.vfx().vfxRef = asset::AssetRef::fromGUID(
                asset::AssetDatabase::instance().registerAsset("tests/preview_smoke.vfvfx", resource::AssetType::VFX));
            handler.update(0.016f);
            CHECK(log.destroyed == std::vector<services::VFXInstanceId>{first});
            REQUIRE(log.created.size() == 2);
            CHECK(log.created[1].params.vfxAssetPath == torch.vfx().vfxRef.resolve());
        }

        SUBCASE("a loop change recreates the instance")
        {
            torch.vfx().loop = !torch.vfx().loop;
            handler.update(0.016f);
            CHECK(log.destroyed == std::vector<services::VFXInstanceId>{first});
            REQUIRE(log.created.size() == 2);
            CHECK(log.created[1].params.loop == torch.vfx().loop);
        }

        SUBCASE("a moved entity moves the instance")
        {
            const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            torch.entity.getComponent<components::WorldTransformComponent>().worldMatrix = world;
            handler.update(0.016f);
            REQUIRE(log.moved.size() == 1);
            CHECK(log.moved[0].first == first);
            CHECK(log.moved[0].second == world);
            CHECK(log.created.size() == 1);
        }

        SUBCASE("removing the component ends the preview")
        {
            torch.entity.removeComponent<components::VFXComponent>();
            handler.update(0.016f);
            CHECK(log.destroyed == std::vector<services::VFXInstanceId>{first});
            CHECK(handler.list(services::EntityHandle::invalid()).empty());
        }
    }

    TEST_CASE("Edit->Play ends every preview immediately")
    {
        asset::AssetDatabase::instance().clear();
        RuntimeLog log;
        RuntimeMocks mocks(log);
        services::VFXEditPreviewHandler handler;
        handler.subscribeToEvents();
        PreviewEntity a("PreviewPlayA");
        PreviewEntity b("PreviewPlayB");

        REQUIRE(handler.start(a.handle).started);
        REQUIRE(handler.start(b.handle).started);

        publishMode(EditorMode::Edit, EditorMode::Play);
        CHECK(log.destroyed.size() == 2); // no update needed
        CHECK(handler.list(services::EntityHandle::invalid()).empty());

        // Back in edit mode a new preview works again.
        publishMode(EditorMode::Play, EditorMode::Edit);
        CHECK(handler.start(a.handle).started);
    }

    TEST_CASE("start is refused in Play mode")
    {
        asset::AssetDatabase::instance().clear();
        RuntimeLog log;
        RuntimeMocks mocks(log);
        services::VFXEditPreviewHandler handler;
        PreviewEntity torch("PreviewInPlay");

        log.playMode = true;
        const services::VFXEditPreviewResult result = handler.start(torch.handle);
        CHECK_FALSE(result.started);
        CHECK_FALSE(result.error.empty());
        CHECK(log.created.empty());
    }

    TEST_CASE("start refuses entities that cannot preview")
    {
        asset::AssetDatabase::instance().clear();
        RuntimeLog log;
        RuntimeMocks mocks(log);
        services::VFXEditPreviewHandler handler;

        CHECK_FALSE(handler.start(services::EntityHandle::invalid()).started);

        scene::Entity bare("PreviewNoComponent");
        const services::EntityHandle bareHandle = services::internal::toHandle(bare.getHandle());
        CHECK_FALSE(handler.start(bareHandle).started);

        bare.addComponent<components::VFXComponent>(); // no effect assigned
        CHECK_FALSE(handler.start(bareHandle).started);
        CHECK(log.created.empty());

        scene::EntityRegistry::getRegistry().destroy(bare.getHandle());
    }

    TEST_CASE("scene clear and entity deletion end previews on the next update")
    {
        asset::AssetDatabase::instance().clear();
        RuntimeLog log;
        RuntimeMocks mocks(log);
        services::VFXEditPreviewHandler handler;
        handler.subscribeToEvents();
        PreviewEntity a("PreviewClearA");
        PreviewEntity b("PreviewClearB");
        REQUIRE(handler.start(a.handle).started);
        REQUIRE(handler.start(b.handle).started);
        const services::VFXInstanceId idA = log.created[0].id;

        SUBCASE("entity deletion")
        {
            ::events::scene::EntityDeletedNotification n;
            n.entity = a.handle;
            ::events::EventDispatcher::instance().publish(n);
            CHECK(log.destroyed.empty()); // queued only
            CHECK(handler.list(services::EntityHandle::invalid()).size() == 1); // reported as stopping

            handler.update(0.016f);
            CHECK(log.destroyed == std::vector<services::VFXInstanceId>{idA});
            CHECK(handler.list(services::EntityHandle::invalid()).size() == 1);
        }

        SUBCASE("scene clear")
        {
            ::events::EventDispatcher::instance().publish(::events::scene::SceneClearedNotification{});
            CHECK(log.destroyed.empty());
            CHECK(handler.list(services::EntityHandle::invalid()).empty());

            handler.update(0.016f);
            CHECK(log.destroyed.size() == 2);
        }

        SUBCASE("a start after a scene clear survives the queued stop")
        {
            ::events::EventDispatcher::instance().publish(::events::scene::SceneClearedNotification{});
            REQUIRE(handler.start(a.handle).started); // flushes the clear first
            CHECK(log.destroyed.size() == 2);
            const services::VFXInstanceId restarted = log.created.back().id;

            handler.update(0.016f);
            const auto previews = handler.list(services::EntityHandle::invalid());
            REQUIRE(previews.size() == 1);
            CHECK(previews[0].instanceId == restarted);
        }
    }

    TEST_CASE("a save of the effect recreates the preview one update later")
    {
        asset::AssetDatabase::instance().clear();
        RuntimeLog log;
        RuntimeMocks mocks(log);
        services::VFXEditPreviewHandler handler;
        handler.subscribeToEvents();
        PreviewEntity torch("PreviewSaved");
        REQUIRE(handler.start(torch.handle).started);
        const services::VFXInstanceId first = log.created[0].id;

        ::events::resource::AssetSavedNotification saved;
        saved.filePath = torch.vfx().vfxRef.resolve();
        ::events::EventDispatcher::instance().publish(saved);

        handler.update(0.016f); // the runtime drops the stale config during this frame
        CHECK(log.created.size() == 1);
        handler.update(0.016f);
        CHECK(log.destroyed == std::vector<services::VFXInstanceId>{first});
        CHECK(log.created.size() == 2);

        handler.update(0.016f); // once only
        CHECK(log.created.size() == 2);
    }

    TEST_CASE("stop ends one preview or all of them")
    {
        asset::AssetDatabase::instance().clear();
        RuntimeLog log;
        RuntimeMocks mocks(log);
        services::VFXEditPreviewHandler handler;
        PreviewEntity a("PreviewStopA");
        PreviewEntity b("PreviewStopB");
        PreviewEntity c("PreviewStopC");
        REQUIRE(handler.start(a.handle).started);
        REQUIRE(handler.start(b.handle).started);
        REQUIRE(handler.start(c.handle).started);

        CHECK(handler.stop(a.handle) == 1u);
        CHECK(handler.stop(a.handle) == 0u);
        CHECK(handler.stop(services::EntityHandle::invalid()) == 2u);
        CHECK(log.destroyed.size() == 3);
        CHECK(handler.list(services::EntityHandle::invalid()).empty());
    }
}
