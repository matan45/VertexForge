#include "BillboardComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"

namespace services {

    BillboardComponentService::BillboardComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph)) {}

    bool BillboardComponentService::addBillboardComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        // Only add if entity doesn't already have a user-created billboard
        if (sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& existing = sceneEntity.getComponent<components::BillboardComponent>();
            if (!existing.editorOnly) {
                return false; // Already has a user billboard
            }
            // Convert existing editor-only billboard to user billboard
            existing.editorOnly = false;
            existing.sizeMode = components::BillboardSizeMode::WorldSpace;
            existing.size = glm::vec2(1.0f, 1.0f);
            return true;
        }

        auto& billboard = sceneEntity.addComponent<components::BillboardComponent>();
        billboard.editorOnly = false;
        billboard.selectable = true;
        billboard.sizeMode = components::BillboardSizeMode::WorldSpace;
        billboard.size = glm::vec2(1.0f, 1.0f);
        billboard.iconType = components::BillboardIconType::Billboard;
        return true;
    }

    bool BillboardComponentService::removeBillboardComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::BillboardComponent>()) {
            auto& billboard = sceneEntity.getComponent<components::BillboardComponent>();
            if (!billboard.editorOnly) {
                sceneEntity.removeComponent<components::BillboardComponent>();
                return true;
            }
        }
        return false;
    }

    bool BillboardComponentService::hasBillboardComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BillboardComponent>()) {
            return false;
        }

        // Only report user-created billboards (not debug icons)
        const auto& billboard = sceneEntity.getComponent<components::BillboardComponent>();
        return !billboard.editorOnly;
    }

    std::optional<BillboardData> BillboardComponentService::getBillboardData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BillboardComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::BillboardComponent>();
        if (comp.editorOnly) {
            return std::nullopt;
        }

        BillboardData data;
        data.texturePath = comp.texturePath;
        data.size = comp.size;
        data.colorTint = comp.colorTint;
        if (comp.renderTextureSource != entt::null)
            data.renderTextureSource = EntityHandle{static_cast<uint64_t>(comp.renderTextureSource)};
        else
            data.renderTextureSource = EntityHandle::invalid();
        data.renderTextureSourceName = comp.renderTextureSourceName;
        return data;
    }

    bool BillboardComponentService::setBillboardData(EntityHandle entity, const BillboardData& billboardData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BillboardComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::BillboardComponent>();
        if (comp.editorOnly) {
            return false;
        }

        comp.texturePath = billboardData.texturePath;
        comp.size = billboardData.size;
        comp.colorTint = billboardData.colorTint;
        comp.renderTextureSourceName = billboardData.renderTextureSourceName;

        // Resolve renderTextureSourceName → entity handle
        comp.renderTextureSource = entt::null;
        if (!comp.renderTextureSourceName.empty()) {
            auto nameView = registry.view<components::NameComponent, components::RenderTextureComponent>();
            for (auto e : nameView) {
                if (nameView.get<components::NameComponent>(e).name == comp.renderTextureSourceName) {
                    comp.renderTextureSource = e;
                    break;
                }
            }
        }
        return true;
    }

    void BillboardComponentService::registerEventHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::scene::AddBillboardComponentCommand>(
            [this](const events::scene::AddBillboardComponentCommand& cmd) {
                return addBillboardComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveBillboardComponentCommand>(
            [this](const events::scene::RemoveBillboardComponentCommand& cmd) {
                return removeBillboardComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetBillboardDataCommand>(
            [this](const events::scene::SetBillboardDataCommand& cmd) {
                return setBillboardData(cmd.entity, cmd.billboardData);
            });

        dispatcher.registerQueryHandler<events::scene::HasBillboardComponentQuery>(
            [this](const events::scene::HasBillboardComponentQuery& query) {
                return hasBillboardComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetBillboardDataQuery>(
            [this](const events::scene::GetBillboardDataQuery& query) {
                return getBillboardData(query.entity);
            });
    }

}
