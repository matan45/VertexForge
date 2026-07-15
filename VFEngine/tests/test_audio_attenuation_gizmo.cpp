#include <doctest.h>
#include <windows/viewport/AudioAttenuationGizmoMath.hpp>
#include <windows/viewport/AudioSource3DDistanceUndo.hpp>
#include <impl/components/AudioComponentService.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <data/EntityConversion.hpp>
#include <events/EventDispatcher.hpp>
#include <events/scene/ComponentPhysicsLightEvents.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <memory>
#include <vector>

// VK-1522: pure screen-space attenuation-handle math plus the narrow
// AudioSource3D distance mutation and value-only undo path.

namespace
{
    using windows::audioattenuation::Distances;
    using windows::audioattenuation::HandleKind;

    constexpr float epsilon = 1e-4f;

    struct CameraMatrices
    {
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f),
                                    glm::vec3(0.0f),
                                    glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = []
        {
            glm::mat4 result = glm::perspective(glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 100.0f);
            result[1][1] *= -1.0f;
            return result;
        }();
    };

    services::AudioComponentService& audioService()
    {
        static auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        static services::AudioComponentService service(sceneGraph);
        static bool registered = false;
        if (!registered)
        {
            registered = true;
            service.registerEventHandlers(events::EventDispatcher::instance());
        }
        return service;
    }

    struct TestEntities
    {
        entt::registry& registry = scene::EntityRegistry::getRegistry();
        std::vector<entt::entity> entities;

        services::EntityHandle create(bool withAudio)
        {
            const entt::entity entity = registry.create();
            entities.push_back(entity);
            if (withAudio)
            {
                registry.emplace<components::AudioSource3DComponent>(entity);
            }
            return services::internal::toHandle(entity);
        }

        ~TestEntities()
        {
            for (const entt::entity entity : entities)
            {
                if (registry.valid(entity))
                {
                    registry.destroy(entity);
                }
            }
        }
    };
}

TEST_SUITE("AudioAttenuationGizmo")
{
    TEST_CASE("Vulkan projection honors viewport origin and opposite equator handles")
    {
        const CameraMatrices camera;
        const glm::vec2 viewportPosition(100.0f, 50.0f);
        const glm::vec2 viewportSize(800.0f, 600.0f);

        const auto center = windows::audioattenuation::projectWorldToScreen(
            glm::vec3(0.0f), camera.view, camera.projection, viewportPosition, viewportSize);
        REQUIRE(center.has_value());
        CHECK(center->x == doctest::Approx(500.0f));
        CHECK(center->y == doctest::Approx(350.0f));

        const auto layout = windows::audioattenuation::buildHandleLayout(
            glm::vec3(0.0f), Distances{1.0f, 2.0f}, camera.view, camera.projection,
            viewportPosition, viewportSize);
        REQUIRE(layout.has_value());
        CHECK(layout->minVisible);
        CHECK(layout->maxVisible);
        CHECK(layout->minHandle.x > center->x);
        CHECK(layout->maxHandle.x < center->x);
        CHECK(layout->minHandle.y == doctest::Approx(center->y));
        CHECK(layout->maxHandle.y == doctest::Approx(center->y));
    }

    TEST_CASE("projection rejects invalid viewport and points behind camera")
    {
        const CameraMatrices camera;
        CHECK_FALSE(windows::audioattenuation::projectWorldToScreen(
            glm::vec3(0.0f), camera.view, camera.projection,
            glm::vec2(0.0f), glm::vec2(0.0f, 600.0f)).has_value());
        CHECK_FALSE(windows::audioattenuation::projectWorldToScreen(
            glm::vec3(0.0f, 0.0f, 10.0f), camera.view, camera.projection,
            glm::vec2(0.0f), glm::vec2(800.0f, 600.0f)).has_value());
    }

    TEST_CASE("hit testing uses fixed pixels and deterministically resolves coincident handles")
    {
        windows::audioattenuation::HandleLayout layout;
        layout.minVisible = true;
        layout.maxVisible = true;
        layout.minHandle = glm::vec2(100.0f, 100.0f);
        layout.maxHandle = glm::vec2(140.0f, 100.0f);

        CHECK(windows::audioattenuation::hitTest(layout, glm::vec2(109.0f, 100.0f), 10.0f) ==
              HandleKind::MinDistance);
        CHECK(windows::audioattenuation::hitTest(layout, glm::vec2(151.0f, 100.0f), 10.0f) ==
              HandleKind::None);

        layout.maxHandle = layout.minHandle;
        CHECK(windows::audioattenuation::hitTest(layout, layout.minHandle, 10.0f) ==
              HandleKind::MaxDistance);
    }

    TEST_CASE("ray-plane solving returns signed world radius and rejects parallel rays")
    {
        const glm::vec3 center(0.0f);
        const glm::vec3 planeNormal(0.0f, 0.0f, 1.0f);
        const math::Ray rightRay(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(3.0f, 0.0f, -5.0f));
        const auto rightRadius = windows::audioattenuation::radiusAlongAxis(
            rightRay, center, planeNormal, glm::vec3(1.0f, 0.0f, 0.0f));
        REQUIRE(rightRadius.has_value());
        CHECK(*rightRadius == doctest::Approx(3.0f).epsilon(epsilon));

        const math::Ray leftRay(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(-4.0f, 0.0f, -5.0f));
        const auto leftRadius = windows::audioattenuation::radiusAlongAxis(
            leftRay, center, planeNormal, glm::vec3(-1.0f, 0.0f, 0.0f));
        REQUIRE(leftRadius.has_value());
        CHECK(*leftRadius == doctest::Approx(4.0f).epsilon(epsilon));

        const math::Ray parallelRay(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        CHECK_FALSE(windows::audioattenuation::radiusAlongAxis(
            parallelRay, center, planeNormal, glm::vec3(1.0f, 0.0f, 0.0f)).has_value());
    }

    TEST_CASE("drag constraints preserve nonnegative ordered distances")
    {
        auto result = windows::audioattenuation::constrainDraggedDistance(
            HandleKind::MinDistance, 12.0f, Distances{1.0f, 10.0f});
        REQUIRE(result.has_value());
        CHECK(result->minDistance == 10.0f);
        CHECK(result->maxDistance == 10.0f);

        result = windows::audioattenuation::constrainDraggedDistance(
            HandleKind::MinDistance, -5.0f, Distances{1.0f, 10.0f});
        REQUIRE(result.has_value());
        CHECK(result->minDistance == 0.0f);
        CHECK(result->maxDistance == 10.0f);

        result = windows::audioattenuation::constrainDraggedDistance(
            HandleKind::MaxDistance, 0.5f, Distances{2.0f, 10.0f});
        REQUIRE(result.has_value());
        CHECK(result->minDistance == 2.0f);
        CHECK(result->maxDistance == 2.0f);

        CHECK_FALSE(windows::audioattenuation::constrainDraggedDistance(
            HandleKind::MaxDistance, std::numeric_limits<float>::infinity(),
            Distances{1.0f, 10.0f}).has_value());
    }

    TEST_CASE("narrow service mutation changes only distances")
    {
        TestEntities fixture;
        auto& service = audioService();
        const services::EntityHandle entity = fixture.create(true);
        auto& component = fixture.registry.get<components::AudioSource3DComponent>(
            services::internal::fromHandle(entity));
        component.volume = 0.35f;
        component.pitch = 1.25f;
        component.loop = true;
        component.showDebugSpheres = true;

        CHECK(service.setAudioSource3DDistances(entity, 3.0f, 45.0f));
        CHECK(component.minDistance == 3.0f);
        CHECK(component.maxDistance == 45.0f);
        CHECK(component.volume == 0.35f);
        CHECK(component.pitch == 1.25f);
        CHECK(component.loop);
        CHECK(component.showDebugSpheres);
    }

    TEST_CASE("narrow service rejects invalid values and never creates a component")
    {
        TestEntities fixture;
        auto& service = audioService();
        const services::EntityHandle audioEntity = fixture.create(true);
        const services::EntityHandle plainEntity = fixture.create(false);

        CHECK_FALSE(service.setAudioSource3DDistances(audioEntity, -1.0f, 10.0f));
        CHECK_FALSE(service.setAudioSource3DDistances(audioEntity, 20.0f, 10.0f));
        CHECK_FALSE(service.setAudioSource3DDistances(
            audioEntity, std::numeric_limits<float>::quiet_NaN(), 10.0f));
        CHECK_FALSE(service.setAudioSource3DDistances(plainEntity, 1.0f, 10.0f));
        CHECK_FALSE(fixture.registry.all_of<components::AudioSource3DComponent>(
            services::internal::fromHandle(plainEntity)));
    }

    TEST_CASE("distance undo replays exact pairs and cannot resurrect a removed component")
    {
        TestEntities fixture;
        audioService();
        const services::EntityHandle entity = fixture.create(true);
        auto& component = fixture.registry.get<components::AudioSource3DComponent>(
            services::internal::fromHandle(entity));
        component.minDistance = 1.0f;
        component.maxDistance = 100.0f;

        windows::AudioSource3DDistanceUndoCommand command(entity, 1.0f, 100.0f, 5.0f, 25.0f);
        command.execute();
        CHECK(component.minDistance == 5.0f);
        CHECK(component.maxDistance == 25.0f);
        command.undo();
        CHECK(component.minDistance == 1.0f);
        CHECK(component.maxDistance == 100.0f);

        fixture.registry.remove<components::AudioSource3DComponent>(
            services::internal::fromHandle(entity));
        CHECK_NOTHROW(command.execute());
        CHECK_FALSE(fixture.registry.all_of<components::AudioSource3DComponent>(
            services::internal::fromHandle(entity)));
    }
}
