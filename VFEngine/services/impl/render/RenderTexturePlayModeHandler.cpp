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

        auto view = registry.view<components::CameraComponent, components::RenderTextureComponent,
                                   components::TransformComponent>();

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

            rendertexture::RenderTextureDesc desc;
            desc.width = rtComp.width;
            desc.height = rtComp.height;
            desc.updateMode = rtComp.updateMode;
            desc.fixedIntervalSeconds = rtComp.fixedIntervalSeconds;
            desc.clearColor = rtComp.clearColor;
            desc.priority = rtComp.priority;
            desc.renderShadows = rtComp.renderShadows;

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
            if (!registry.valid(enttEntity))
                continue;

            if (!registry.all_of<components::CameraComponent>(enttEntity) ||
                !registry.all_of<components::TransformComponent>(enttEntity))
                continue;

            auto& camera = registry.get<components::CameraComponent>(enttEntity);

            // Use the world-space eye position (to support child cameras that inherit parent movement)
            // with the camera's LOCAL Euler rotation. Decomposing/inverting the world matrix for the
            // rotation hits the extractEulerAngleXYZ yaw singularity and flips the view to the sky past
            // ±90° of yaw (VK-1350).
            const auto& transform = registry.get<components::TransformComponent>(enttEntity);
            glm::vec3 worldPos;
            if (registry.all_of<components::WorldTransformComponent>(enttEntity)) {
                const auto& worldTransform = registry.get<components::WorldTransformComponent>(enttEntity);
                camera.updateViewMatrixFromWorldEye(worldTransform.worldMatrix, transform);
                worldPos = glm::vec3(worldTransform.worldMatrix[3]);
            } else {
                camera.updateViewMatrix(transform.position, transform.rotation);
                worldPos = transform.position;
            }
            glm::mat4 worldViewMatrix = camera.viewMatrix;

            if (registry.all_of<components::RenderTextureComponent>(enttEntity))
            {
                const auto& rtComp = registry.get<components::RenderTextureComponent>(enttEntity);
                float rttAspect = static_cast<float>(rtComp.width) / static_cast<float>(rtComp.height);
                if (camera.aspectRatio != rttAspect)
                {
                    camera.aspectRatio = rttAspect;
                    camera.updateProjectionMatrix();
                }
            }

            provider->updateCamera(
                textureId,
                worldViewMatrix,
                camera.projectionMatrix,
                worldPos,
                camera.nearPlane,
                camera.farPlane
            );
        }

        provider->renderAll(deltaTime);
    }
}
