#include "TextComponentService.hpp"
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
        autoAttachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::Text));
        return true;
    }

    bool TextComponentService::removeTextComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::TextComponent>()) {
            autoDetachBillboard(entity, static_cast<uint32_t>(components::BillboardIconType::Text));
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
        data.fontStyle = static_cast<uint8_t>(comp.fontStyle);
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
        comp.fontStyle = static_cast<components::FontStyle>(textData.fontStyle);
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

    void TextComponentService::autoAttachBillboard(EntityHandle entity, uint32_t iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.addComponent<components::BillboardComponent>();
            billboard.iconType = static_cast<components::BillboardIconType>(iconType);
            billboard.editorOnly = true;
            billboard.selectable = true;
        }
    }

    void TextComponentService::autoDetachBillboard(EntityHandle entity, uint32_t iconType) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.getComponent<components::BillboardComponent>();
            if (billboard.iconType == static_cast<components::BillboardIconType>(iconType)) {
                sceneEntity.removeComponent<components::BillboardComponent>();
            }
        }
    }

}
