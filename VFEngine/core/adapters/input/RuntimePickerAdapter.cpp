#include "RuntimePickerAdapter.hpp"

#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/scene/EntityTransformEvents.hpp" // GetPrimaryCameraQuery
#include "../../../services/events/project/ApplicationEvents.hpp"   // GetViewportWidth/HeightQuery
#include "../../../services/events/terrain/TerrainEvents.hpp"       // GetTerrainHeightAtQuery
#include "../../../services/events/physics/PhysicsEvents.hpp"       // RaycastQuery

#include <glm/glm.hpp>
#include <cmath>

namespace core
{
    namespace
    {
        constexpr float PICK_MAX_DISTANCE = 5000.0f;
        constexpr float TERRAIN_COARSE_STEP = 1.0f;
        constexpr int TERRAIN_BISECT_ITERS = 24;
        constexpr float TERRAIN_EPS = 1e-4f;
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

    std::optional<glm::vec3> RuntimePickerAdapter::pickTerrain(const services::PickRay& ray)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        auto heightAt = [&](float x, float z, bool& valid) -> float
        {
            events::terrain::GetTerrainHeightAtQuery q;
            q.worldX = x;
            q.worldZ = z;
            auto r = dispatcher.query(q);
            valid = r.valid;
            return r.height;
        };

        // f(t) = rayY(t) - terrainY(t); a downward ray over terrain starts positive.
        auto f = [&](float t) -> float
        {
            glm::vec3 p = ray.origin + ray.direction * t;
            bool valid = false;
            return p.y - heightAt(p.x, p.z, valid);
        };

        float prevT = 0.0f;
        float prevF = f(0.0f);

        for (float t = TERRAIN_COARSE_STEP; t <= PICK_MAX_DISTANCE; t += TERRAIN_COARSE_STEP)
        {
            float ft = f(t);
            if (prevF > 0.0f && ft <= 0.0f)
            {
                // Crossing bracketed in [prevT, t]; refine by bisection.
                float lo = prevT;
                float hi = t;
                for (int i = 0; i < TERRAIN_BISECT_ITERS; ++i)
                {
                    float mid = 0.5f * (lo + hi);
                    float fm = f(mid);
                    if (std::fabs(fm) < TERRAIN_EPS)
                    {
                        lo = hi = mid;
                        break;
                    }
                    if (fm > 0.0f)
                        lo = mid;
                    else
                        hi = mid;
                }

                float tHit = 0.5f * (lo + hi);
                glm::vec3 hit = ray.origin + ray.direction * tHit;
                bool valid = false;
                hit.y = heightAt(hit.x, hit.z, valid);
                if (!valid)
                    return std::nullopt; // off the loaded terrain
                return hit;
            }
            prevT = t;
            prevF = ft;
        }

        return std::nullopt;
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
}
