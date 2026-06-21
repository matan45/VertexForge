#include "RenderTextureComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"

namespace services {

    RenderTextureComponentService::RenderTextureComponentService(
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    bool RenderTextureComponentService::addRenderTextureComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::RenderTextureComponent>()) {
            sceneEntity.addComponent<components::RenderTextureComponent>();
            return true;
        }
        return false;
    }

    bool RenderTextureComponentService::removeRenderTextureComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::RenderTextureComponent>()) {
            sceneEntity.removeComponent<components::RenderTextureComponent>();
            return true;
        }
        return false;
    }

    bool RenderTextureComponentService::hasRenderTextureComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::RenderTextureComponent>();
    }

    std::optional<RenderTextureData> RenderTextureComponentService::getRenderTextureData(
        EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::RenderTextureComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::RenderTextureComponent>();
        RenderTextureData data;
        data.width = comp.width;
        data.height = comp.height;
        data.updateMode = static_cast<uint8_t>(comp.updateMode);
        data.fixedIntervalSeconds = comp.fixedIntervalSeconds;
        data.clearColor = comp.clearColor;
        data.priority = comp.priority;
        data.enabled = comp.enabled;
        data.renderShadows = comp.renderShadows;
        // VK-1414: source-camera reference. The name is the serialized identity; the handle mirrors
        // the resolved entt entity (mirrors BillboardComponentService renderTextureSource mapping).
        data.sourceCameraName = comp.sourceCameraName;
        if (comp.sourceCamera != entt::null)
            data.sourceCamera = EntityHandle{static_cast<uint64_t>(comp.sourceCamera)};
        else
            data.sourceCamera = EntityHandle::invalid();
        data.runtimeTextureId = comp.textureId;
        return data;
    }

    bool RenderTextureComponentService::setRenderTextureData(
        EntityHandle entity, const RenderTextureData& renderTextureData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::RenderTextureComponent>()) {
            sceneEntity.addComponent<components::RenderTextureComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::RenderTextureComponent>();
        comp.width = renderTextureData.width;
        comp.height = renderTextureData.height;
        comp.updateMode = static_cast<rendertexture::UpdateMode>(renderTextureData.updateMode);
        comp.fixedIntervalSeconds = renderTextureData.fixedIntervalSeconds;
        comp.clearColor = renderTextureData.clearColor;
        comp.priority = renderTextureData.priority;
        comp.enabled = renderTextureData.enabled;
        comp.renderShadows = renderTextureData.renderShadows;
        // VK-1414: persist the source-camera name and eager-resolve it to a CAMERA entity handle.
        // textureId is intentionally NOT touched here (runtime-owned).
        comp.sourceCameraName = renderTextureData.sourceCameraName;
        comp.sourceCamera = entt::null;
        if (!comp.sourceCameraName.empty()) {
            auto camView = registry.view<components::NameComponent, components::CameraComponent>();
            for (auto e : camView) {
                if (camView.get<components::NameComponent>(e).name == comp.sourceCameraName) {
                    comp.sourceCamera = e;
                    break;
                }
            }
        }
        return true;
    }

    void RenderTextureComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::scene::AddRenderTextureComponentCommand>(
            [this](const events::scene::AddRenderTextureComponentCommand& cmd) {
                return addRenderTextureComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveRenderTextureComponentCommand>(
            [this](const events::scene::RemoveRenderTextureComponentCommand& cmd) {
                return removeRenderTextureComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetRenderTextureDataCommand>(
            [this](const events::scene::SetRenderTextureDataCommand& cmd) {
                return setRenderTextureData(cmd.entity, cmd.renderTextureData);
            });

        dispatcher.registerQueryHandler<events::scene::HasRenderTextureComponentQuery>(
            [this](const events::scene::HasRenderTextureComponentQuery& query) {
                return hasRenderTextureComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetRenderTextureDataQuery>(
            [this](const events::scene::GetRenderTextureDataQuery& query) {
                return getRenderTextureData(query.entity);
            });
    }

}
