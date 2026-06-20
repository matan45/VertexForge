#include "RuntimePickerAdapter.hpp"

#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/scene/EntityTransformEvents.hpp" // GetPrimaryCameraQuery
#include "../../../services/events/project/ApplicationEvents.hpp"   // GetViewportWidth/HeightQuery
#include "../../../services/events/physics/PhysicsEvents.hpp"       // RaycastQuery
#include "math/ScreenRegionFrustum.hpp"                              // buildScreenRegionFrustum, classifyPointInFrustum

#include <glm/glm.hpp>

namespace core
{
    namespace
    {
        constexpr float PICK_MAX_DISTANCE = 5000.0f;
    }

    bool RuntimePickerAdapter::screenToWorldRay(const glm::vec2& screenPos, services::PickRay& outRay)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        auto camEntity = dispatcher.query(events::scene::GetPrimaryCameraQuery{});
        if (!camEntity.has_value())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity cam = services::internal::fromHandle(*camEntity);
        if (!registry.valid(cam) || !registry.all_of<components::CameraComponent>(cam))
            return false;

        const auto& camComp = registry.get<components::CameraComponent>(cam);

        float w = static_cast<float>(dispatcher.query(events::application::GetViewportWidthQuery{}));
        float h = static_cast<float>(dispatcher.query(events::application::GetViewportHeightQuery{}));
        if (w <= 0.0f || h <= 0.0f)
            return false;

        // viewportPos is (0,0) in standalone runtime; screenPos is already viewport-relative.
        float normalizedX = glm::clamp(screenPos.x, 0.0f, w) / w;
        float normalizedY = glm::clamp(screenPos.y, 0.0f, h) / h;

        // No manual Y flip: the runtime projection uses the Vulkan flipped-Y convention,
        // matching the editor ViewPortPicker.
        float ndcX = normalizedX * 2.0f - 1.0f;
        float ndcY = normalizedY * 2.0f - 1.0f;

        glm::mat4 invProj = glm::inverse(camComp.projectionMatrix);
        glm::mat4 invView = glm::inverse(camComp.viewMatrix);

        glm::vec4 nearPoint = invProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        glm::vec4 farPoint = invProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        glm::vec3 worldNear = glm::vec3(invView * nearPoint);
        glm::vec3 worldFar = glm::vec3(invView * farPoint);

        outRay.origin = worldNear;
        outRay.direction = glm::normalize(worldFar - worldNear);
        return true;
    }

    bool RuntimePickerAdapter::worldToScreen(const glm::vec3& worldPos, glm::vec2& outScreen)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        auto camEntity = dispatcher.query(events::scene::GetPrimaryCameraQuery{});
        if (!camEntity.has_value())
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity cam = services::internal::fromHandle(*camEntity);
        if (!registry.valid(cam) || !registry.all_of<components::CameraComponent>(cam))
            return false;

        const auto& camComp = registry.get<components::CameraComponent>(cam);

        float w = static_cast<float>(dispatcher.query(events::application::GetViewportWidthQuery{}));
        float h = static_cast<float>(dispatcher.query(events::application::GetViewportHeightQuery{}));
        if (w <= 0.0f || h <= 0.0f)
            return false;

        // Exact inverse of screenToWorldRay's NDC mapping: no manual Y flip — the
        // runtime projection already uses the Vulkan flipped-Y convention.
        glm::vec4 clip = camComp.projectionMatrix * camComp.viewMatrix * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 1e-6f)
            return false; // behind the camera

        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        outScreen.x = (ndc.x * 0.5f + 0.5f) * w;
        outScreen.y = (ndc.y * 0.5f + 0.5f) * h;
        return true;
    }

    services::RaycastHit RuntimePickerAdapter::pickEntity(const services::PickRay& ray, uint16_t layerMask)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::physics::RaycastQuery q;
        q.origin = ray.origin;
        q.direction = ray.direction;
        q.maxDistance = PICK_MAX_DISTANCE;
        q.layerMask = layerMask;
        return dispatcher.query(q);
    }

    std::vector<services::EntityHandle> RuntimePickerAdapter::pickRegion(const glm::vec2& minPx,
                                                                        const glm::vec2& maxPx,
                                                                        uint16_t /*layerMask*/)
    {
        // layerMask is accepted for API symmetry with pickEntity but is not applied:
        // there is no cheap per-entity physics layer here (entities carry only a
        // TransformComponent in the general case). Scripts post-filter the returned
        // ids by their own gameplay components (e.g. Selectable).
        std::vector<services::EntityHandle> result;

        auto& dispatcher = events::EventDispatcher::instance();

        auto camEntity = dispatcher.query(events::scene::GetPrimaryCameraQuery{});
        if (!camEntity.has_value())
            return result;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity cam = services::internal::fromHandle(*camEntity);
        if (!registry.valid(cam) || !registry.all_of<components::CameraComponent>(cam))
            return result;

        const auto& camComp = registry.get<components::CameraComponent>(cam);

        float w = static_cast<float>(dispatcher.query(events::application::GetViewportWidthQuery{}));
        float h = static_cast<float>(dispatcher.query(events::application::GetViewportHeightQuery{}));
        if (w <= 0.0f || h <= 0.0f)
            return result;

        // Build the 6 world-space frustum planes for the screen sub-rectangle. The
        // helper normalizes inverted drags and uses the same pixel->NDC mapping as
        // screenToWorldRay (no manual Y flip).
        const math::Frustum frustum = math::buildScreenRegionFrustum(
            camComp.viewMatrix, camComp.projectionMatrix, minPx, maxPx, w, h);

        // Center-point selection: VertexForge has no cached per-entity world-space
        // AABB component, so resolving a tight bounds per entity would require a
        // synchronous mesh-resource lookup. Testing TransformComponent.position
        // against the frustum is adequate for RTS drag-select.
        auto view = registry.view<components::TransformComponent>();
        for (entt::entity e : view)
        {
            const auto& transform = view.get<components::TransformComponent>(e);
            if (math::classifyPointInFrustum(frustum, transform.position))
                result.push_back(services::internal::toHandle(e));
        }

        return result;
    }
}
