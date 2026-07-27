#include "TextComponentService.hpp"
#include "BillboardAutoIcon.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"

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
        components_helpers::autoAttachBillboard(entity, components::BillboardIconType::Text);
        return true;
    }

    bool TextComponentService::removeTextComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::TextComponent>()) {
            components_helpers::autoDetachBillboard(entity, components::BillboardIconType::Text);
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
        data.fontRef = comp.fontRef;
        data.text = comp.text;
        data.fontSize = comp.fontSize;
        data.color = comp.color;
        data.lineSpacing = comp.lineSpacing;
        data.letterSpacing = comp.letterSpacing;
        data.maxWidth = comp.maxWidth;
        data.rectHeight = comp.rectHeight;
        data.fontStyle = static_cast<uint8_t>(comp.fontStyle);
        data.horizontalAlignment = static_cast<uint8_t>(comp.horizontalAlignment);
        data.verticalAlignment = static_cast<uint8_t>(comp.verticalAlignment);
        data.overflow = static_cast<uint8_t>(comp.overflow);
        data.wordWrap = comp.wordWrap;
        data.effects = comp.effects;
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
        comp.fontRef = textData.fontRef;
        comp.text = textData.text;
        comp.fontSize = textData.fontSize;
        comp.color = textData.color;
        comp.lineSpacing = textData.lineSpacing;
        comp.letterSpacing = textData.letterSpacing;
        comp.maxWidth = textData.maxWidth;
        comp.rectHeight = textData.rectHeight;
        comp.fontStyle = static_cast<components::FontStyle>(textData.fontStyle);
        comp.horizontalAlignment = static_cast<components::HorizontalAlignment>(textData.horizontalAlignment);
        comp.verticalAlignment = static_cast<components::VerticalAlignment>(textData.verticalAlignment);
        comp.overflow = static_cast<components::TextOverflow>(textData.overflow);
        comp.wordWrap = textData.wordWrap;
        comp.effects = textData.effects;
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
