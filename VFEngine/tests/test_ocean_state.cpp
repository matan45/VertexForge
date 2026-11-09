// VK-1658: OceanService's whole-ocean state (GetOceanStateQuery / SetOceanStateCommand) and what
// OceanComponent::isActive means now: the ocean SURFACE is visible. A hidden ocean keeps holding the
// scene's water look, level and buoyancy tuning, but has no surface, no FFT and no height answers
// outside the water bodies.
//
// Drives a real OceanService over its own SceneGraphSystem, on the shared EnTT registry and a
// dispatcher that is cleared before and after every case. No physics provider and no terrain
// service are registered: the service treats both as optional.

#include <doctest.h>

#include <impl/scene/OceanService.hpp>
#include <events/EventDispatcher.hpp>
#include <events/terrain/OceanEvents.hpp>
#include <events/scene/EntityTransformEvents.hpp>
#include <data/EntityConversion.hpp>
#include <data/OceanData.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <serialization/SceneSerialization.hpp>
#include <asset/AssetDatabase.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>

namespace
{
    namespace fs = std::filesystem;

    // Other suites leave water in the shared registry (test_sector_assignment keeps an
    // OceanComponent, test_water_body keeps active bodies on its scene roots). These cases need the
    // registry to hold exactly the water they create, whatever order doctest runs the suites in.
    void clearStrayWater()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        registry.clear<components::OceanComponent>();
        registry.clear<components::WaterBodyComponent>();
    }

    struct DispatcherReset
    {
        DispatcherReset() { events::EventDispatcher::instance().clear(); }
        ~DispatcherReset() { events::EventDispatcher::instance().clear(); }
    };

    // Members are destroyed in reverse order: the scene is emptied first (destructor body), then the
    // service unregisters its handlers, and only then is the dispatcher cleared.
    struct OceanFixture
    {
        DispatcherReset dispatcherReset;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::OceanService service{sceneGraph};

        OceanFixture()
        {
            clearStrayWater();
            service.registerEventHandlers();
        }

        ~OceanFixture()
        {
            sceneGraph->clearScene();
            clearStrayWater();
        }

        // The editor's creation path (OceanEditorWindow::createOcean).
        services::EntityHandle createOcean(float level = 0.0f, bool fftFlag = true) const
        {
            events::ocean::CreateOceanCommand command;
            command.config.waterHeight = level;
            command.config.oceanConfig.enabled = fftFlag;
            return events::EventDispatcher::instance().execute(command);
        }

        services::EntityHandle createBody(const glm::vec3& position, const glm::vec2& halfExtents, float depth) const
        {
            events::ocean::CreateWaterBodyCommand command;
            command.position = position;
            command.data.halfExtents = halfExtents;
            command.data.depth = depth;
            return events::EventDispatcher::instance().execute(command);
        }
    };

    std::optional<services::OceanStateData> readState(services::EntityHandle entity = {})
    {
        events::ocean::GetOceanStateQuery query;
        query.entity = entity;
        return events::EventDispatcher::instance().query(query);
    }

    bool writeState(const services::OceanStateData& state, services::EntityHandle entity = {})
    {
        events::ocean::SetOceanStateCommand command;
        command.entity = entity;
        command.state = state;
        return events::EventDispatcher::instance().execute(command);
    }

    bool fftEnabled()
    {
        return events::EventDispatcher::instance().query(events::ocean::IsOceanFFTEnabledQuery{});
    }

    std::size_t oceanComponentCount()
    {
        std::size_t count = 0;
        for (auto entity : scene::EntityRegistry::getRegistry().view<components::OceanComponent>())
        {
            (void)entity;
            ++count;
        }
        return count;
    }

    // Every field away from its default and from the others, so a swapped or dropped member cannot
    // hide behind an equal value.
    services::OceanStateData distinctState()
    {
        services::OceanStateData state;
        state.waterHeight = -3.5f;
        state.isActive = true;

        services::OceanVisualSettings& v = state.visual;
        v.shallowColor = glm::vec4(0.11f, 0.12f, 0.13f, 0.14f);
        v.deepColor = glm::vec4(0.21f, 0.22f, 0.23f, 0.24f);
        v.maxVisibleDepth = 31.0f;
        v.fresnelPower = 3.25f;
        v.refractionStrength = 1.25f;
        v.refractionChromatic = 0.35f;
        v.refractionDepthScale = 0.45f;
        v.causticStrength = 2.5f;
        v.causticDepthFalloff = 0.75f;
        v.shoreFoamRange = 4.5f;
        v.shoreFoamIntensity = 0.55f;
        v.shoreBreakingStrength = 1.15f;
        v.shoreWetRange = 6.5f;
        v.ssrEnabled = true;
        v.ssrIntensity = 0.65f;
        v.ssrMaxDistance = 140.0f;
        v.ssrThickness = 0.85f;
        v.ssrMaxSteps = 40;
        v.ssrDebugView = true;
        v.beerLambertEnabled = true;
        v.absorptionCoeff = glm::vec3(0.31f, 0.32f, 0.33f);
        v.scatteringColor = glm::vec3(0.41f, 0.42f, 0.43f);
        v.scatterCoeff = 0.15f;
        v.absorptionMaxDistance = 45.0f;
        v.hexTilingEnabled = true;
        v.hexBandMask = 0x5u;
        v.hexCellScale = 2.25f;
        v.hexBlendContrast = 7.5f;
        v.shoalingEnabled = true;
        v.shoalingStrength = 0.65f;
        v.shoalingMinDepth = 1.75f;
        v.shoalingWavelengthScale = 1.85f;
        v.shoalingGamma = 0.95f;
        v.shoreEdgeFadeStart = 0.77f;
        v.shoreWavesEnabled = true;
        v.shoreWaveAmplitude = 1.35f;
        v.shoreWaveLength = 17.0f;
        v.shoreWaveSpeed = 0.95f;
        v.shoreWaveBreakDepth = 2.75f;
        v.shoreWaveBreakRange = 1.65f;
        v.shoreWaveCrestFoam = 0.85f;
        v.shoreWaveCrestFoamThreshold = 0.45f;
        v.shoreWaveLean = 1.45f;
        v.rippleSimEnabled = true;
        v.ripplePatchSize = 160.0f;
        v.rippleWaveSpeed = 4.5f;
        v.rippleDamping = 1.6f;
        v.rippleHeightScale = 2.1f;
        v.rippleNormalScale = 2.2f;
        v.rippleFoamGain = 0.35f;
        v.rippleFoamScale = 1.9f;
        v.rippleFoamDecay = 2.6f;
        v.rippleEdgeFadeStart = 0.66f;

        state.physics.density = 1025.0f;
        state.physics.drag = 1.4f;
        state.physics.buoyancyStrength = 3.3f;
        state.physics.physicsEnabled = false;

        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            const float k = static_cast<float>(i + 1);
            services::OceanBandConfigData& band = state.bands[i];
            band.resolution = 64u << i;   // 64, 128, 256
            band.patchSize = 150.0f * k;
            band.windSpeed = 6.5f * k;
            band.windDirection = 33.0f * k;
            band.amplitude = 0.00002f * k;
            band.choppiness = 0.9f * k;
            band.foamThreshold = -0.35f + 0.1f * k;
            band.displacementScale = 2.5f * k;
            band.enabled = i != 1;
            band.foamPersistence = 0.1f * k;
            band.foamDecay = 0.7f * k;
        }
        state.gravity = 9.5f;

        state.weatherDriven = false;   // true would make the next tick ask a weather service
        state.weatherResponse = 1.35f;
        state.currentBeaufort = 7.25f;
        return state;
    }

    fs::path oceanStateTestRoot()
    {
        return fs::temp_directory_path() / "vf_ocean_state_tests";
    }
}

TEST_SUITE("OceanState")
{
    TEST_CASE("without an ocean there is no state, the write is refused and the FFT is off")
    {
        OceanFixture fixture;

        CHECK_FALSE(readState().has_value());
        CHECK_FALSE(writeState(services::OceanStateData{}));
        CHECK_FALSE(fftEnabled());
        CHECK_FALSE(fixture.service.hasActiveOcean());
        CHECK_FALSE(fixture.service.hasWaterToRender());

        // SetOceanFFTConfig cannot switch the FFT on with no ocean to run it for.
        events::ocean::SetOceanFFTConfigCommand config;
        config.config.enabled = true;
        events::EventDispatcher::instance().execute(config);
        CHECK_FALSE(fftEnabled());
    }

    TEST_CASE("a created ocean is visible, runs the FFT and reports its creation data")
    {
        OceanFixture fixture;

        const services::EntityHandle ocean = fixture.createOcean(-6.0f);
        REQUIRE(ocean.isValid());

        const auto state = readState();
        REQUIRE(state.has_value());
        CHECK(state->waterHeight == doctest::Approx(-6.0f));
        CHECK(state->isActive);
        CHECK(state->physics.physicsEnabled);
        // OceanFFTConfigData's swell band, not the component default (500 m): the editor's path.
        CHECK(state->bands[0].patchSize == doctest::Approx(100.0f));
        CHECK(fftEnabled());
        CHECK(fixture.service.hasActiveOcean());
        CHECK(fixture.service.hasWaterToRender());

        // Addressing the ocean by its id answers the same.
        const auto byEntity = readState(ocean);
        REQUIRE(byEntity.has_value());
        CHECK(*byEntity == *state);
    }

    TEST_CASE("createOcean runs the FFT even when the caller left the flag off")
    {
        OceanFixture fixture;

        REQUIRE(fixture.createOcean(0.0f, /*fftFlag=*/false).isValid());
        CHECK(fftEnabled());
        CHECK(fixture.service.hasActiveOcean());
    }

    TEST_CASE("a second ocean is refused")
    {
        OceanFixture fixture;

        const services::EntityHandle first = fixture.createOcean(1.0f);
        REQUIRE(first.isValid());
        CHECK_FALSE(fixture.createOcean(2.0f).isValid());

        CHECK(oceanComponentCount() == 1);
        CHECK(events::EventDispatcher::instance().query(events::ocean::GetOceanEntityQuery{}).id == first.id);
        CHECK(readState()->waterHeight == doctest::Approx(1.0f));
    }

    TEST_CASE("every field round-trips through SetOceanState")
    {
        OceanFixture fixture;

        const services::EntityHandle ocean = fixture.createOcean();
        REQUIRE(ocean.isValid());

        const services::OceanStateData written = distinctState();
        CHECK(writeState(written, ocean));

        const auto read = readState();
        REQUIRE(read.has_value());
        CHECK(*read == written);

        // The FFT config follows the written bands and gravity.
        const auto config = events::EventDispatcher::instance().query(events::ocean::GetOceanFFTConfigQuery{});
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
            CHECK(config.bands[i] == written.bands[i]);
        CHECK(config.gravity == doctest::Approx(written.gravity));
        CHECK(config.enabled);

        // ... and the existing per-part readers see the same values.
        CHECK(fixture.service.getOceanVisualSettings() == written.visual);
        CHECK(fixture.service.getBaseWaterHeight() == doctest::Approx(written.waterHeight));
        events::ocean::GetOceanDataQuery dataQuery;
        dataQuery.entity = ocean;
        const auto data = events::EventDispatcher::instance().query(dataQuery);
        REQUIRE(data.has_value());
        CHECK(data->physicsEnabled == written.physics.physicsEnabled);
        CHECK(data->weatherResponse == doctest::Approx(written.weatherResponse));
        CHECK(data->currentBeaufort == doctest::Approx(written.currentBeaufort));
    }

    TEST_CASE("another entity's state is neither read nor written")
    {
        OceanFixture fixture;

        REQUIRE(fixture.createOcean().isValid());
        const auto before = readState();
        REQUIRE(before.has_value());

        auto& registry = scene::EntityRegistry::getRegistry();
        const entt::entity other = registry.create();
        const services::EntityHandle otherHandle = services::internal::toHandle(other);

        CHECK_FALSE(readState(otherHandle).has_value());
        services::OceanStateData changed = *before;
        changed.waterHeight = 42.0f;
        CHECK_FALSE(writeState(changed, otherHandle));
        CHECK(*readState() == *before);

        registry.destroy(other);
    }

    TEST_CASE("a hidden ocean keeps its settings but has no surface, no FFT and no height answers")
    {
        OceanFixture fixture;

        const services::EntityHandle ocean = fixture.createOcean(2.0f);
        REQUIRE(ocean.isValid());
        services::OceanStateData state = *readState();
        state.visual.shoreFoamRange = 6.0f;
        REQUIRE(writeState(state));

        // Visible: a point below the level is in the sea.
        CHECK(fixture.service.isPositionInOcean(glm::vec3(0.0f, -5.0f, 0.0f)));
        CHECK(fixture.service.getWaterSurfaceAt(glm::vec3(0.0f, -5.0f, 0.0f)).has_value());

        const uint32_t versionBefore = fixture.service.getOceanFFTConfigVersion();
        state.isActive = false;
        REQUIRE(writeState(state));

        CHECK_FALSE(fixture.service.hasActiveOcean());
        CHECK_FALSE(fftEnabled());
        CHECK(fixture.service.getOceanFFTConfigVersion() != versionBefore);
        CHECK_FALSE(fixture.service.hasWaterToRender());

        CHECK_FALSE(fixture.service.isPositionInOcean(glm::vec3(0.0f, -5.0f, 0.0f)));
        CHECK_FALSE(fixture.service.getWaterSurfaceAt(glm::vec3(0.0f, -5.0f, 0.0f)).has_value());
        CHECK(fixture.service.getOceanHeightAt(glm::vec2(0.0f)) == 0.0f);
        CHECK(fixture.service.getWaterDepthAt(glm::vec2(0.0f)) == 0.0f);

        // ... while it is still the scene's ocean and its water-look holder.
        CHECK(events::EventDispatcher::instance().query(events::ocean::GetOceanEntityQuery{}).id == ocean.id);
        const auto hidden = readState();
        REQUIRE(hidden.has_value());
        CHECK(*hidden == state);
        CHECK(fixture.service.getOceanVisualSettings().shoreFoamRange == doctest::Approx(6.0f));
        CHECK(fixture.service.getBaseWaterHeight() == doctest::Approx(2.0f));

        events::ocean::GetOceanDataQuery dataQuery;
        dataQuery.entity = ocean;
        const auto data = events::EventDispatcher::instance().query(dataQuery);
        REQUIRE(data.has_value());
        CHECK_FALSE(data->isActive);
        CHECK_FALSE(data->oceanConfig.enabled);
    }

    TEST_CASE("water bodies keep answering while the ocean is hidden")
    {
        OceanFixture fixture;

        REQUIRE(fixture.createOcean(0.0f).isValid());
        services::OceanStateData state = *readState();
        state.isActive = false;
        REQUIRE(writeState(state));

        // A lake whose surface is at y = 1 and floor at y = -2, centred on (10, 0).
        REQUIRE(fixture.createBody(glm::vec3(10.0f, 1.0f, 0.0f), glm::vec2(5.0f), 3.0f).isValid());

        CHECK(fixture.service.hasWaterToRender());
        CHECK(fixture.service.isPositionInOcean(glm::vec3(10.0f, 0.5f, 0.0f)));
        const auto surface = fixture.service.getWaterSurfaceAt(glm::vec3(10.0f, 0.5f, 0.0f));
        REQUIRE(surface.has_value());
        CHECK(*surface == doctest::Approx(1.0f));
        CHECK(fixture.service.getOceanHeightAt(glm::vec2(10.0f, 0.0f)) == doctest::Approx(1.0f));

        // Outside the lake there is no ocean to fall back to.
        CHECK_FALSE(fixture.service.isPositionInOcean(glm::vec3(30.0f, -5.0f, 0.0f)));
        CHECK_FALSE(fixture.service.getWaterSurfaceAt(glm::vec3(30.0f, -5.0f, 0.0f)).has_value());
    }

    TEST_CASE("showing a hidden ocean again restarts the FFT")
    {
        OceanFixture fixture;

        REQUIRE(fixture.createOcean().isValid());
        services::OceanStateData state = *readState();

        state.isActive = false;
        REQUIRE(writeState(state));
        CHECK_FALSE(fftEnabled());

        const uint32_t hiddenVersion = fixture.service.getOceanFFTConfigVersion();
        state.isActive = true;
        REQUIRE(writeState(state));
        CHECK(fftEnabled());
        CHECK(fixture.service.hasActiveOcean());
        CHECK(fixture.service.getOceanFFTConfigVersion() != hiddenVersion);
    }

    TEST_CASE("SetOceanFFTConfig cannot start the FFT of a hidden ocean but still stores its bands")
    {
        OceanFixture fixture;

        REQUIRE(fixture.createOcean().isValid());
        services::OceanStateData state = *readState();
        state.isActive = false;
        REQUIRE(writeState(state));

        events::ocean::SetOceanFFTConfigCommand command;
        command.config = events::EventDispatcher::instance().query(events::ocean::GetOceanFFTConfigQuery{});
        command.config.enabled = true;
        command.config.bands[0].patchSize = 321.0f;
        events::EventDispatcher::instance().execute(command);

        CHECK_FALSE(fftEnabled());
        CHECK(readState()->bands[0].patchSize == doctest::Approx(321.0f));
        CHECK_FALSE(readState()->isActive);
    }

    TEST_CASE("the FFT config version moves only when the bands, the gravity or the visibility change")
    {
        OceanFixture fixture;

        REQUIRE(fixture.createOcean().isValid());
        services::OceanStateData state = *readState();
        uint32_t version = fixture.service.getOceanFFTConfigVersion();

        int configNotifications = 0;
        auto& dispatcher = events::EventDispatcher::instance();
        const auto token = dispatcher.subscribe<events::ocean::OceanFFTConfigChangedNotification>(
            [&configNotifications](const events::ocean::OceanFFTConfigChangedNotification&) { ++configNotifications; });

        SUBCASE("rewriting the same state")
        {
            REQUIRE(writeState(state));
            CHECK(fixture.service.getOceanFFTConfigVersion() == version);
            CHECK(configNotifications == 0);
        }
        SUBCASE("a look-only change")
        {
            state.visual.fresnelPower = 7.0f;
            state.waterHeight = 3.0f;
            state.physics.drag = 2.0f;
            REQUIRE(writeState(state));
            CHECK(fixture.service.getOceanFFTConfigVersion() == version);
            CHECK(configNotifications == 0);
        }
        SUBCASE("a band change")
        {
            state.bands[1].windSpeed = 21.0f;
            REQUIRE(writeState(state));
            CHECK(fixture.service.getOceanFFTConfigVersion() == version + 1);
            CHECK(configNotifications == 1);
        }
        SUBCASE("a gravity change")
        {
            state.gravity = 9.0f;
            REQUIRE(writeState(state));
            CHECK(fixture.service.getOceanFFTConfigVersion() == version + 1);
            CHECK(configNotifications == 1);
        }

        dispatcher.unsubscribe(token);
    }

    TEST_CASE("a manual sea-state transition survives a look change and is cancelled by a band change")
    {
        OceanFixture fixture;

        REQUIRE(fixture.createOcean().isValid());
        auto& dispatcher = events::EventDispatcher::instance();
        const float startBeaufort = readState()->currentBeaufort;

        events::ocean::SetOceanSeaStateCommand seaState;
        seaState.beaufort = startBeaufort + 4.0f;
        seaState.transitionSeconds = 10.0f;
        dispatcher.execute(seaState);

        // A look change mid-transition leaves the transition running.
        services::OceanStateData state = *readState();
        state.visual.shoreFoamRange = 9.0f;
        REQUIRE(writeState(state));

        events::ocean::UpdateOceanCommand tick;
        tick.deltaTime = 5.0f;
        dispatcher.execute(tick);
        CHECK(readState()->currentBeaufort == doctest::Approx(startBeaufort + 2.0f));

        // New bands cancel it: the next tick must not lerp them away.
        state = *readState();
        state.bands[0].windSpeed = 20.0f;
        REQUIRE(writeState(state));
        dispatcher.execute(tick);

        const auto after = readState();
        REQUIRE(after.has_value());
        CHECK(after->bands[0].windSpeed == doctest::Approx(20.0f));
        CHECK(after->currentBeaufort == doctest::Approx(state.currentBeaufort));
    }

    TEST_CASE("an applied band config cancels a manual sea-state transition")
    {
        OceanFixture fixture;

        REQUIRE(fixture.createOcean().isValid());
        auto& dispatcher = events::EventDispatcher::instance();

        events::ocean::SetOceanSeaStateCommand seaState;
        seaState.beaufort = 10.0f;
        seaState.transitionSeconds = 10.0f;
        dispatcher.execute(seaState);

        // The Ocean Editor's "Apply Ocean": explicit bands win over the running transition.
        events::ocean::SetOceanFFTConfigCommand config;
        config.config = dispatcher.query(events::ocean::GetOceanFFTConfigQuery{});
        config.config.bands[0].windSpeed = 33.0f;
        dispatcher.execute(config);

        events::ocean::UpdateOceanCommand tick;
        tick.deltaTime = 5.0f;
        dispatcher.execute(tick);
        CHECK(readState()->bands[0].windSpeed == doctest::Approx(33.0f));
    }

    TEST_CASE("a new ocean does not resume a deleted ocean's sea-state transition")
    {
        OceanFixture fixture;
        auto& dispatcher = events::EventDispatcher::instance();

        const services::EntityHandle first = fixture.createOcean();
        REQUIRE(first.isValid());
        events::ocean::SetOceanSeaStateCommand seaState;
        seaState.beaufort = 10.0f;
        seaState.transitionSeconds = 10.0f;
        dispatcher.execute(seaState);

        events::ocean::DeleteOceanCommand remove;
        remove.oceanEntity = first;
        REQUIRE(dispatcher.execute(remove));

        REQUIRE(fixture.createOcean().isValid());
        const services::OceanStateData created = *readState();

        events::ocean::UpdateOceanCommand tick;
        tick.deltaTime = 5.0f;
        dispatcher.execute(tick);
        CHECK(*readState() == created);
    }

    TEST_CASE("hiding the last water abandons a shore bake in flight, a lake keeps it running")
    {
        OceanFixture fixture;

        REQUIRE(fixture.createOcean().isValid());
        auto& dispatcher = events::EventDispatcher::instance();
        const auto baking = [&dispatcher]()
        {
            return dispatcher.query(events::ocean::GetShoreDepthFieldStatusQuery{}).baking;
        };

        SUBCASE("no water left: nothing would drive the bake any more")
        {
            fixture.service.updateShoreDepthField(glm::vec2(0.0f));
            REQUIRE(baking());

            services::OceanStateData state = *readState();
            state.isActive = false;
            REQUIRE(writeState(state));
            CHECK_FALSE(fixture.service.hasWaterToRender());
            CHECK_FALSE(baking());
        }
        SUBCASE("a lake still renders: the bake keeps going")
        {
            REQUIRE(fixture.createBody(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(5.0f), 3.0f).isValid());
            fixture.service.updateShoreDepthField(glm::vec2(0.0f));
            REQUIRE(baking());

            services::OceanStateData state = *readState();
            state.isActive = false;
            REQUIRE(writeState(state));
            CHECK(fixture.service.hasWaterToRender());
            CHECK(baking());
        }
    }

    TEST_CASE("a state write asks for a shore-field rebake")
    {
        OceanFixture fixture;

        REQUIRE(fixture.createOcean().isValid());
        auto& dispatcher = events::EventDispatcher::instance();
        const glm::vec2 camera(0.0f);

        // Run the first bake to completion (no terrain service: a field of "no bottom").
        fixture.service.updateShoreDepthField(camera);
        for (int tick = 0; tick < 32 && dispatcher.query(events::ocean::GetShoreDepthFieldStatusQuery{}).baking; ++tick)
            fixture.service.updateShoreDepthField(camera);
        REQUIRE_FALSE(dispatcher.query(events::ocean::GetShoreDepthFieldStatusQuery{}).baking);

        // Nothing moved: no new bake.
        fixture.service.updateShoreDepthField(camera);
        CHECK_FALSE(dispatcher.query(events::ocean::GetShoreDepthFieldStatusQuery{}).baking);

        // A look-only write (same level, same camera) still starts one: sculpting publishes nothing,
        // so an ocean edit is when the field picks the terrain up again.
        services::OceanStateData state = *readState();
        state.visual.shoreFoamIntensity = 0.3f;
        REQUIRE(writeState(state));
        fixture.service.updateShoreDepthField(camera);
        CHECK(dispatcher.query(events::ocean::GetShoreDepthFieldStatusQuery{}).baking);
    }

    TEST_CASE("deleting the ocean entity drops it, publishes OceanDeleted and abandons the shore bake")
    {
        OceanFixture fixture;

        const services::EntityHandle ocean = fixture.createOcean();
        REQUIRE(ocean.isValid());
        auto& dispatcher = events::EventDispatcher::instance();

        // A bake spans several ticks; leave one in flight.
        fixture.service.updateShoreDepthField(glm::vec2(0.0f));
        REQUIRE(dispatcher.query(events::ocean::GetShoreDepthFieldStatusQuery{}).baking);

        int deleted = 0;
        const auto token = dispatcher.subscribe<events::ocean::OceanDeletedNotification>(
            [&deleted, ocean](const events::ocean::OceanDeletedNotification& n)
            {
                if (n.oceanEntity.id == ocean.id)
                    ++deleted;
            });

        // What HierarchyService's DeleteEntityCommand publishes for every entity of the subtree.
        events::scene::EntityDeletedNotification notification;
        notification.entity = ocean;
        dispatcher.publish(notification);

        CHECK(deleted == 1);
        CHECK_FALSE(dispatcher.query(events::ocean::GetOceanEntityQuery{}).isValid());
        CHECK_FALSE(readState().has_value());
        CHECK_FALSE(fftEnabled());
        CHECK_FALSE(dispatcher.query(events::ocean::GetShoreDepthFieldStatusQuery{}).baking);

        dispatcher.unsubscribe(token);
    }

    TEST_CASE("a hidden ocean survives a scene save, reload and rebuild")
    {
        OceanFixture fixture;

        std::error_code ec;
        fs::remove_all(oceanStateTestRoot(), ec);
        fs::create_directories(oceanStateTestRoot(), ec);
        asset::AssetDatabase::instance().clear();

        REQUIRE(fixture.createOcean().isValid());
        services::OceanStateData saved = distinctState();
        saved.isActive = false;
        REQUIRE(writeState(saved));

        const fs::path scenePath = oceanStateTestRoot() / "HiddenOcean.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(*fixture.sceneGraph, scenePath.string()));

        // The load replaces the scene (the ocean entity dies without a notification, as on a real
        // load); RebuildOceanFromComponents is what ScenePersistenceService runs after it.
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), *fixture.sceneGraph));
        REQUIRE(oceanComponentCount() == 1);
        events::EventDispatcher::instance().execute(events::ocean::RebuildOceanFromComponentsCommand{});

        const auto loaded = readState();
        REQUIRE(loaded.has_value());
        CHECK(*loaded == saved);
        CHECK_FALSE(fixture.service.hasActiveOcean());
        CHECK_FALSE(fftEnabled());
    }

    TEST_CASE("stateFromComponent and applyState are exact inverses over the right members")
    {
        const services::OceanStateData state = distinctState();

        components::OceanComponent component;
        services::OceanService::applyState(component, state);
        CHECK(services::OceanService::stateFromComponent(component) == state);

        // Spot-check the member mapping itself, which the inverse property alone would not catch if
        // both directions swapped the same two members.
        CHECK(component.waterHeight == state.waterHeight);
        CHECK(component.isActive == state.isActive);
        CHECK(component.density == state.physics.density);
        CHECK(component.drag == state.physics.drag);
        CHECK(component.buoyancyStrength == state.physics.buoyancyStrength);
        CHECK(component.physicsEnabled == state.physics.physicsEnabled);
        CHECK(component.shallowColor == state.visual.shallowColor);
        CHECK(component.deepColor == state.visual.deepColor);
        CHECK(component.maxVisibleDepth == state.visual.maxVisibleDepth);
        CHECK(component.shoreFoamRange == state.visual.shoreFoamRange);
        CHECK(component.shoreWetRange == state.visual.shoreWetRange);
        CHECK(component.ssrMaxSteps == state.visual.ssrMaxSteps);
        CHECK(component.hexBandMask == state.visual.hexBandMask);
        CHECK(component.shoalingGamma == state.visual.shoalingGamma);
        CHECK(component.shoreWaveLean == state.visual.shoreWaveLean);
        CHECK(component.rippleEdgeFadeStart == state.visual.rippleEdgeFadeStart);
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            CHECK(component.oceanBands[i].resolution == state.bands[i].resolution);
            CHECK(component.oceanBands[i].windDirection == state.bands[i].windDirection);
            CHECK(component.oceanBands[i].amplitude == state.bands[i].amplitude);
            CHECK(component.oceanBands[i].enabled == state.bands[i].enabled);
            CHECK(component.oceanBands[i].foamDecay == state.bands[i].foamDecay);
        }
        CHECK(component.oceanGravity == state.gravity);
        CHECK(component.weatherDriven == state.weatherDriven);
        CHECK(component.weatherResponse == state.weatherResponse);
        CHECK(component.currentBeaufort == state.currentBeaufort);
    }

    TEST_CASE("SetOceanVisualSettings still writes every visual field")
    {
        OceanFixture fixture;

        const services::EntityHandle ocean = fixture.createOcean();
        REQUIRE(ocean.isValid());

        const services::OceanVisualSettings visual = distinctState().visual;
        events::ocean::SetOceanVisualSettingsCommand command;
        command.oceanEntity = ocean;
        command.settings = visual;
        events::EventDispatcher::instance().execute(command);

        events::ocean::GetOceanVisualSettingsQuery query;
        query.entity = ocean;
        const auto read = events::EventDispatcher::instance().query(query);
        REQUIRE(read.has_value());
        CHECK(*read == visual);
    }
}
