#include "RenderTexturePlayModeHandler.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/render/RenderTextureEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include <components/CoreComponents.hpp>
#include "print/Log.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace services
{
    RenderTexturePlayModeHandler::RenderTexturePlayModeHandler(IRenderTextureProvider* provider)
        : provider(provider)
    {
    }

    RenderTexturePlayModeHandler::~RenderTexturePlayModeHandler()
    {
        unsubscribeFromEvents();
    }

    void RenderTexturePlayModeHandler::subscribeToEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        editorModeChangedToken = dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& notification)
            {
                onEditorModeChanged(notification.previousMode, notification.currentMode);
            });
    }

    void RenderTexturePlayModeHandler::unsubscribeFromEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (editorModeChangedToken.isValid())
        {
            dispatcher.unsubscribe(editorModeChangedToken);
            editorModeChangedToken = {};
        }
    }

    void RenderTexturePlayModeHandler::onEditorModeChanged(EditorMode previousMode, EditorMode currentMode)
    {
        if (previousMode == EditorMode::Edit && currentMode == EditorMode::Play)
        {
            enterPlayMode();
        }
        else if (previousMode == EditorMode::Play && currentMode == EditorMode::Edit)
        {
            exitPlayMode();
        }
    }

    void RenderTexturePlayModeHandler::enterPlayMode()
    {
        if (!provider)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();

        // Iterate every RTT entity (not just self-camera RTTs): an RTT may render from a SEPARATE
        // camera entity referenced via sourceCamera (VK-1414), so a view that requires a co-located
        // CameraComponent would silently skip those.
        auto view = registry.view<components::RenderTextureComponent>();

        for (auto entity : view)
        {
            auto& rtComp = view.get<components::RenderTextureComponent>(entity);

            if (!rtComp.enabled)
                continue;

            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                    continue;
            }

            // Need either this entity's own camera+transform, or a valid source-camera entity.
            const bool hasOwnCamera =
                registry.all_of<components::CameraComponent, components::TransformComponent>(entity);
            const bool hasSourceCamera =
                rtComp.sourceCamera != entt::null && registry.valid(rtComp.sourceCamera) &&
                registry.all_of<components::CameraComponent, components::TransformComponent>(rtComp.sourceCamera);
            if (!hasOwnCamera && !hasSourceCamera)
                continue;

            rendertexture::RenderTextureDesc desc;
            desc.width = rtComp.width;
            desc.height = rtComp.height;
            desc.updateMode = rtComp.updateMode;
            desc.fixedIntervalSeconds = rtComp.fixedIntervalSeconds;
            desc.clearColor = rtComp.clearColor;
            desc.priority = rtComp.priority;
            desc.renderShadows = rtComp.renderShadows;
            desc.tonemap = rtComp.tonemap;

            rendertexture::RenderTextureId textureId = provider->createRenderTexture(desc);

            if (textureId != rendertexture::INVALID_RENDER_TEXTURE_ID)
            {
                EntityHandle handle = internal::toHandle(entity);
                activeTextures[handle] = textureId;
                rtComp.textureId = textureId;
            }
        }

        rttActive = true;
    }

    void RenderTexturePlayModeHandler::exitPlayMode()
    {
        if (!provider)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();

        for (const auto& [handle, textureId] : activeTextures)
        {
            provider->destroyRenderTexture(textureId);

            auto enttEntity = internal::fromHandle(handle);
            if (registry.valid(enttEntity) && registry.all_of<components::RenderTextureComponent>(enttEntity))
            {
                auto& rtComp = registry.get<components::RenderTextureComponent>(enttEntity);
                rtComp.textureId = rendertexture::INVALID_RENDER_TEXTURE_ID;
                rtComp.needsRender = true;
                rtComp.timeSinceLastRender = 0.0f;
            }
        }

        activeTextures.clear();
        rttActive = false;
    }

    void RenderTexturePlayModeHandler::update(float deltaTime)
    {
        if (!rttActive || !provider)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();

        for (const auto& [handle, textureId] : activeTextures)
        {
            auto enttEntity = internal::fromHandle(handle);
            if (!registry.valid(enttEntity) ||
                !registry.all_of<components::RenderTextureComponent>(enttEntity))
                continue;

            const auto& rtComp = registry.get<components::RenderTextureComponent>(enttEntity);

            // Render from the referenced source camera (VK-1414) when it is valid; otherwise fall
            // back silently to this RTT entity's own camera (legacy / stale-handle).
            entt::entity camEntity = enttEntity;
            if (rtComp.sourceCamera != entt::null && registry.valid(rtComp.sourceCamera) &&
                registry.all_of<components::CameraComponent, components::TransformComponent>(rtComp.sourceCamera))
            {
                camEntity = rtComp.sourceCamera;
            }

            if (!registry.all_of<components::CameraComponent, components::TransformComponent>(camEntity))
                continue;

            // Operate on a VALUE COPY of the source camera so a shared camera (e.g. the player's)
            // is never mutated by the RTT's aspect-ratio / view recompute.
            auto camCopy = registry.get<components::CameraComponent>(camEntity);
            const auto& srcTransform = registry.get<components::TransformComponent>(camEntity);

            glm::vec3 worldPos;
            if (camCopy.viewMatrixOverride) {
                // VK-1416: the source camera's view/projection are script-owned — render exactly those.
                worldPos = glm::vec3(glm::inverse(camCopy.viewMatrix)[3]);
            } else {
                // Use the world-space eye position (to support child cameras that inherit parent movement)
                // with the camera's LOCAL Euler rotation. Decomposing/inverting the world matrix for the
                // rotation hits the extractEulerAngleXYZ yaw singularity and flips the view to the sky past
                // ±90° of yaw (VK-1350).
                if (registry.all_of<components::WorldTransformComponent>(camEntity)) {
                    const auto& worldTransform = registry.get<components::WorldTransformComponent>(camEntity);
                    camCopy.updateViewMatrixFromWorldEye(worldTransform.worldMatrix, srcTransform);
                    worldPos = glm::vec3(worldTransform.worldMatrix[3]);
                } else {
                    camCopy.updateViewMatrix(srcTransform.position, srcTransform.rotation);
                    worldPos = srcTransform.position;
                }

                camCopy.aspectRatio = static_cast<float>(rtComp.width) / static_cast<float>(rtComp.height);
                camCopy.updateProjectionMatrix();
            }

            provider->updateCamera(
                textureId,
                camCopy.viewMatrix,
                camCopy.projectionMatrix,
                worldPos,
                camCopy.nearPlane,
                camCopy.farPlane,
                camCopy.cullingMask
            );
        }

        provider->renderAll(deltaTime);
    }
}
