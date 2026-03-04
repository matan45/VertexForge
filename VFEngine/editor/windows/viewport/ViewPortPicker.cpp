#include "ViewPortPicker.hpp"
#include "../../camera/EditorCamera.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/EntityConversion.hpp"
#include <limits>

namespace windows
{
    void ViewPortPicker::updateBillboardScreenPositions(const editor::EditorCamera& camera,
                                                        glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        cachedBillboardHits.clear();

        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
        {
            return;
        }

        glm::mat4 viewMatrix = camera.getViewMatrix();
        glm::mat4 projMatrix = camera.getProjectionMatrix();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::BillboardComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            const auto& billboard = view.get<components::BillboardComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (!billboard.selectable)
            {
                continue;
            }

            glm::vec3 worldPos = glm::vec3(worldTransform.worldMatrix[3]);

            glm::vec4 clipPos = projMatrix * viewMatrix * glm::vec4(worldPos, 1.0f);

            if (clipPos.w <= 0.0f)
            {
                continue;
            }

            glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

            if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f)
            {
                continue;
            }

            // No Y flip needed because EditorCamera's projection matrix already
            // flips Y for Vulkan (projectionMatrix[1][1] *= -1), so ndc.y = -1 is top, +1 is bottom
            glm::vec2 screenPos;
            screenPos.x = (ndc.x * 0.5f + 0.5f) * viewportSize.x + viewportPos.x;
            screenPos.y = (ndc.y * 0.5f + 0.5f) * viewportSize.y + viewportPos.y;

            BillboardScreenHit hit;
            hit.entity = services::internal::toHandle(entity);
            hit.screenCenter = screenPos;

            if (billboard.sizeMode == components::BillboardSizeMode::WorldSpace)
            {
                // Project world-space size to screen pixels for hit testing
                // Use a point offset by the billboard half-size to estimate screen extent
                glm::vec3 cameraRight = glm::vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
                glm::vec3 cameraUp = glm::vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);

                glm::vec4 rightClip = projMatrix * viewMatrix * glm::vec4(worldPos + cameraRight * billboard.size.x * 0.5f, 1.0f);
                glm::vec4 upClip = projMatrix * viewMatrix * glm::vec4(worldPos + cameraUp * billboard.size.y * 0.5f, 1.0f);

                glm::vec2 rightScreen;
                rightScreen.x = ((rightClip.x / rightClip.w) * 0.5f + 0.5f) * viewportSize.x + viewportPos.x;
                rightScreen.y = ((rightClip.y / rightClip.w) * 0.5f + 0.5f) * viewportSize.y + viewportPos.y;

                glm::vec2 upScreen;
                upScreen.x = ((upClip.x / upClip.w) * 0.5f + 0.5f) * viewportSize.x + viewportPos.x;
                upScreen.y = ((upClip.y / upClip.w) * 0.5f + 0.5f) * viewportSize.y + viewportPos.y;

                float screenWidth = glm::length(rightScreen - screenPos) * 2.0f;
                float screenHeight = glm::length(upScreen - screenPos) * 2.0f;
                hit.screenSize = glm::vec2(screenWidth, screenHeight);
            }
            else
            {
                hit.screenSize = billboard.size; // Size is in screen pixels for ScreenSpace mode
            }

            cachedBillboardHits.push_back(hit);
        }
    }

    std::optional<services::EntityHandle> ViewPortPicker::pickBillboardAt(glm::vec2 screenPos)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // Iterate in reverse order (last rendered = closest to camera for screen-space billboards)
        for (auto it = cachedBillboardHits.rbegin(); it != cachedBillboardHits.rend(); ++it)
        {
            const auto& hit = *it;

            glm::vec2 halfSize = hit.screenSize * 0.5f;
            glm::vec2 minBounds = hit.screenCenter - halfSize;
            glm::vec2 maxBounds = hit.screenCenter + halfSize;

            if (screenPos.x >= minBounds.x && screenPos.x <= maxBounds.x &&
                screenPos.y >= minBounds.y && screenPos.y <= maxBounds.y)
            {
                // Validate entity still exists and has BillboardComponent (prevents race condition
                // if entity was deleted or component removed between cache update and pick)
                auto enttEntity = services::internal::fromHandle(hit.entity);
                if (registry.valid(enttEntity) &&
                    registry.all_of<components::BillboardComponent>(enttEntity))
                {
                    return hit.entity;
                }
            }
        }
        return std::nullopt;
    }

    void ViewPortPicker::updateMeshPickData()
    {
        cachedMeshHits.clear();

        auto& dispatcher = events::EventDispatcher::instance();
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            const auto& meshComp = view.get<components::MeshComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (meshComp.meshPath.empty())
            {
                continue;
            }

            events::render::GetMeshBoundingBoxQuery query;
            query.meshPath = meshComp.meshPath;
            auto bounds = dispatcher.query(query);

            if (!bounds.has_value())
            {
                continue;
            }

            math::AABB localAABB(bounds->min, bounds->max);
            math::AABB worldAABB = localAABB.getTransformed(worldTransform.worldMatrix);

            MeshPickData pickData;
            pickData.entity = services::internal::toHandle(entity);
            pickData.worldAABB = worldAABB;
            pickData.meshPath = meshComp.meshPath;

            cachedMeshHits.push_back(pickData);
        }
    }

    math::Ray ViewPortPicker::screenToWorldRay(const editor::EditorCamera& camera,
                                               glm::vec2 screenPos,
                                               glm::vec2 viewportPos,
                                               glm::vec2 viewportSize)
    {
        screenPos.x = glm::clamp(screenPos.x, viewportPos.x, viewportPos.x + viewportSize.x);
        screenPos.y = glm::clamp(screenPos.y, viewportPos.y, viewportPos.y + viewportSize.y);

        float normalizedX = (screenPos.x - viewportPos.x) / viewportSize.x;
        float normalizedY = (screenPos.y - viewportPos.y) / viewportSize.y;

        // Y is already in correct orientation due to Vulkan's flipped projection
        float ndcX = normalizedX * 2.0f - 1.0f;
        float ndcY = normalizedY * 2.0f - 1.0f;

        glm::mat4 invProj = glm::inverse(camera.getProjectionMatrix());
        glm::mat4 invView = glm::inverse(camera.getViewMatrix());

        glm::vec4 nearPoint = invProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        glm::vec4 farPoint = invProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

        // Perspective divide
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        glm::vec3 worldNear = glm::vec3(invView * nearPoint);
        glm::vec3 worldFar = glm::vec3(invView * farPoint);

        glm::vec3 direction = glm::normalize(worldFar - worldNear);
        return math::Ray(worldNear, direction);
    }

    std::optional<services::EntityHandle> ViewPortPicker::pickMeshAt(const editor::EditorCamera& camera,
                                                                     glm::vec2 screenPos,
                                                                     glm::vec2 viewportPos,
                                                                     glm::vec2 viewportSize)
    {
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
        {
            return std::nullopt;
        }

        auto& registry = scene::EntityRegistry::getRegistry();

        math::Ray ray = screenToWorldRay(camera, screenPos, viewportPos, viewportSize);

        // Collect all hits, then prefer smaller bounding boxes
        // This allows selecting inner objects within larger parent bounds
        float smallestVolume = std::numeric_limits<float>::max();
        std::optional<services::EntityHandle> bestEntity;

        for (const auto& meshData : cachedMeshHits)
        {
            auto hitDistance = meshData.worldAABB.intersectRay(ray);
            if (hitDistance.has_value())
            {
                auto enttEntity = services::internal::fromHandle(meshData.entity);
                if (registry.valid(enttEntity) &&
                    registry.all_of<components::MeshComponent>(enttEntity))
                {
                    float volume = meshData.worldAABB.getVolume();
                    if (volume < smallestVolume)
                    {
                        smallestVolume = volume;
                        bestEntity = meshData.entity;
                    }
                }
            }
        }

        return bestEntity;
    }
}
