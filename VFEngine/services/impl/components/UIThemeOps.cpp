#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "ui/UIThemeApplier.hpp"
#include "ui/UIThemeSerialization.hpp"
#include "print/Log.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UIThemeEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"

namespace services {

    bool UIComponentService::addUIStyleComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::UIStyleComponent>()) return false;
        sceneEntity.addComponent<components::UIStyleComponent>();
        return true;
    }

    bool UIComponentService::removeUIStyleComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIStyleComponent>()) return false;
        sceneEntity.removeComponent<components::UIStyleComponent>();
        return true;
    }

    bool UIComponentService::hasUIStyleComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIStyleComponent>();
    }

    std::optional<std::string> UIComponentService::getUIStyleKey(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return std::nullopt;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIStyleComponent>()) return std::nullopt;
        return sceneEntity.getComponent<components::UIStyleComponent>().styleKey;
    }

    bool UIComponentService::setUIStyleKey(EntityHandle entity, const std::string& styleKey) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIStyleComponent>()) {
            sceneEntity.addComponent<components::UIStyleComponent>();
        }
        sceneEntity.getComponent<components::UIStyleComponent>().styleKey = styleKey;

        // Apply the new style immediately when the owning canvas is themed.
        entt::entity handle = internal::fromHandle(entity);
        entt::entity current = handle;
        while (registry.valid(current)) {
            if (auto* canvas = registry.try_get<components::UICanvasComponent>(current)) {
                if (canvas->themeRef.isValid()) {
                    auto theme = utilities::ui::UIThemeSerialization::loadFromFile(canvas->themeRef.resolve());
                    if (theme.has_value()) {
                        if (const auto* style = theme->findStyle(styleKey)) {
                            utilities::ui::UIThemeApplier::applyStyleToEntity(registry, *style, handle);
                        }
                    }
                }
                break;
            }
            auto* parent = registry.try_get<components::ParentComponent>(current);
            if (!parent || parent->parent == entt::null) break;
            current = parent->parent;
        }
        return true;
    }

    bool UIComponentService::setCanvasTheme(EntityHandle entity, const std::string& themePath) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        entt::entity handle = internal::fromHandle(entity);
        auto* canvas = registry.try_get<components::UICanvasComponent>(handle);
        if (!canvas) return false;

        if (themePath.empty()) {
            canvas->themeRef = asset::AssetRef::invalid();
            return true;
        }

        asset::AssetRef ref = asset::AssetRef::fromPath(themePath);
        if (!ref.isValid()) {
            vfLogWarning("setCanvasTheme: theme asset not found: {}", themePath);
            return false;
        }
        canvas->themeRef = ref;
        return applyCanvasTheme(handle) >= 0;
    }

    std::optional<std::string> UIComponentService::getCanvasThemePath(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return std::nullopt;
        auto* canvas = registry.try_get<components::UICanvasComponent>(internal::fromHandle(entity));
        if (!canvas) return std::nullopt;
        if (!canvas->themeRef.isValid()) return std::string();
        return canvas->themeRef.resolve();
    }

    // Loads and applies a single canvas' theme. Returns touched count, -1 on failure.
    int UIComponentService::applyCanvasTheme(entt::entity canvasEntity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto* canvas = registry.try_get<components::UICanvasComponent>(canvasEntity);
        if (!canvas || !canvas->themeRef.isValid()) return -1;

        const std::string& path = canvas->themeRef.resolve();
        if (path.empty()) {
            vfLogWarning("applyCanvasTheme: theme GUID does not resolve to a file");
            return -1;
        }

        auto theme = utilities::ui::UIThemeSerialization::loadFromFile(path);
        if (!theme.has_value()) return -1;

        return utilities::ui::UIThemeApplier::applyTheme(registry, *theme, canvasEntity);
    }

    int UIComponentService::reapplyUITheme(std::optional<EntityHandle> canvas) {
        auto& registry = scene::EntityRegistry::getRegistry();

        if (canvas.has_value()) {
            if (!internal::isValidHandle(*canvas, registry)) return 0;
            int touched = applyCanvasTheme(internal::fromHandle(*canvas));
            return touched > 0 ? touched : 0;
        }

        int totalTouched = 0;
        for (auto [entity, canvasComp] : registry.view<components::UICanvasComponent>().each()) {
            if (canvasComp.themeRef.isValid()) {
                int touched = applyCanvasTheme(entity);
                if (touched > 0) totalTouched += touched;
            }
        }
        return totalTouched;
    }

    void UIComponentService::registerThemeHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::ui::AddUIStyleComponentCommand>(
            [this](const events::ui::AddUIStyleComponentCommand& cmd) {
                return addUIStyleComponent(cmd.entity);
            });
        dispatcher.registerCommandHandler<events::ui::RemoveUIStyleComponentCommand>(
            [this](const events::ui::RemoveUIStyleComponentCommand& cmd) {
                return removeUIStyleComponent(cmd.entity);
            });
        dispatcher.registerCommandHandler<events::ui::SetUIStyleKeyCommand>(
            [this](const events::ui::SetUIStyleKeyCommand& cmd) {
                return setUIStyleKey(cmd.entity, cmd.styleKey);
            });
        dispatcher.registerCommandHandler<events::ui::SetCanvasThemeCommand>(
            [this](const events::ui::SetCanvasThemeCommand& cmd) {
                return setCanvasTheme(cmd.entity, cmd.themePath);
            });
        dispatcher.registerCommandHandler<events::ui::ReapplyUIThemeCommand>(
            [this](const events::ui::ReapplyUIThemeCommand& cmd) {
                return reapplyUITheme(cmd.canvas);
            });
        dispatcher.registerQueryHandler<events::ui::HasUIStyleComponentQuery>(
            [this](const events::ui::HasUIStyleComponentQuery& query) {
                return hasUIStyleComponent(query.entity);
            });
        dispatcher.registerQueryHandler<events::ui::GetUIStyleKeyQuery>(
            [this](const events::ui::GetUIStyleKeyQuery& query) {
                return getUIStyleKey(query.entity);
            });
        dispatcher.registerQueryHandler<events::ui::GetCanvasThemeQuery>(
            [this](const events::ui::GetCanvasThemeQuery& query) {
                return getCanvasThemePath(query.entity);
            });

        // Themed values are baked into saved scenes, but the theme asset may
        // have changed since the scene was saved — reapply on every load.
        sceneLoadedToken = dispatcher.subscribe<events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification&) {
                reapplyUITheme(std::nullopt);
            });
    }

}
