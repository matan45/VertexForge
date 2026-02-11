#include "TextComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/SceneEvents.hpp"

namespace services {

    TextComponentService::TextComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    bool TextComponentService::addTextComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::TextComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::TextComponent>();
        return true;
    }

    bool TextComponentService::removeTextComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::TextComponent>()) {
            sceneEntity.removeComponent<components::TextComponent>();
            return true;
        }
        return false;
    }

    bool TextComponentService::hasTextComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::TextComponent>();
    }

    std::optional<TextData> TextComponentService::getTextData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::TextComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::TextComponent>();

        TextData data;
        data.fontPath = comp.fontPath;
        data.text = comp.text;
        data.fontSize = comp.fontSize;
        data.color = comp.color;
        data.renderMode = static_cast<uint8_t>(comp.renderMode);
        data.lineSpacing = comp.lineSpacing;
        data.maxWidth = comp.maxWidth;
        return data;
    }

    bool TextComponentService::setTextData(EntityHandle entity, const TextData& textData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::TextComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::TextComponent>();
        comp.fontPath = textData.fontPath;
        comp.text = textData.text;
        comp.fontSize = textData.fontSize;
        comp.color = textData.color;
        comp.renderMode = static_cast<components::TextRenderMode>(textData.renderMode);
        comp.lineSpacing = textData.lineSpacing;
        comp.maxWidth = textData.maxWidth;
        return true;
    }

    void TextComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::scene::AddTextComponentCommand>(
            [this](const events::scene::AddTextComponentCommand& cmd) {
                return addTextComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveTextComponentCommand>(
            [this](const events::scene::RemoveTextComponentCommand& cmd) {
                return removeTextComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetTextDataCommand>(
            [this](const events::scene::SetTextDataCommand& cmd) {
                return setTextData(cmd.entity, cmd.textData);
            });

        dispatcher.registerQueryHandler<events::scene::HasTextComponentQuery>(
            [this](const events::scene::HasTextComponentQuery& query) {
                return hasTextComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetTextDataQuery>(
            [this](const events::scene::GetTextDataQuery& query) {
                return getTextData(query.entity);
            });
    }

}
