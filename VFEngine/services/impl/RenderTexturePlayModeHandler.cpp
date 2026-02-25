#include "RenderTexturePlayModeHandler.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/EditorModeEvents.hpp"
#include "../events/RenderTextureEvents.hpp"
#include "../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include <components/CoreComponents.hpp>
#include "print/Logger.hpp"

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

        // Find all entities with CameraComponent + RenderTextureComponent
        auto view = registry.view<components::CameraComponent, components::RenderTextureComponent,
                                   components::TransformComponent>();

        for (auto entity : view)
        {
            auto& rtComp = view.get<components::RenderTextureComponent>(entity);

            if (!rtComp.enabled)
                continue;

            // Skip inactive entities
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                    continue;
            }

            // Create render texture
            rendertexture::RenderTextureDesc desc;
            desc.width = rtComp.width;
            desc.height = rtComp.height;
            desc.updateMode = rtComp.updateMode;
            desc.fixedIntervalSeconds = rtComp.fixedIntervalSeconds;
            desc.clearColor = rtComp.clearColor;
            desc.priority = rtComp.priority;

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

        // Destroy all active render textures
        for (const auto& [handle, textureId] : activeTextures)
        {
            provider->destroyRenderTexture(textureId);

            // Reset component state
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

        // Update camera data for each active RTT entity
        for (const auto& [handle, textureId] : activeTextures)
        {
            auto enttEntity = internal::fromHandle(handle);
            if (!registry.valid(enttEntity))
                continue;

            if (!registry.all_of<components::CameraComponent>(enttEntity) ||
                !registry.all_of<components::TransformComponent>(enttEntity))
                continue;

            const auto& camera = registry.get<components::CameraComponent>(enttEntity);
            const auto& transform = registry.get<components::TransformComponent>(enttEntity);

            provider->updateCamera(
                textureId,
                camera.viewMatrix,
                camera.projectionMatrix,
                transform.position,
                camera.nearPlane,
                camera.farPlane
            );
        }

        // Render all active textures
        provider->renderAll(deltaTime);
    }
}
