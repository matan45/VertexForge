#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "ui/UIRectMath.hpp"
#include "math/Frustum.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UIEvents.hpp"
#include <limits>

namespace services {

    // ========== UI Label Operations ==========

    bool UIComponentService::addUILabelComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UILabelComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UILabelComponent>();

        // Auto-add UIRectComponent if missing (label needs rect for positioning)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUILabelComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UILabelComponent>();
        return true;
    }

    bool UIComponentService::hasUILabelComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UILabelComponent>();
    }

    std::optional<UILabelData> UIComponentService::getUILabelData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UILabelComponent>();

        UILabelData data;
        data.text = comp.text;
        data.fontRef = comp.fontRef;
        data.fontSize = comp.fontSize;
        data.fontStyle = static_cast<uint8_t>(comp.fontStyle);
        data.color = comp.color;
        data.horizontalAlignment = static_cast<uint8_t>(comp.horizontalAlignment);
        data.verticalAlignment = static_cast<uint8_t>(comp.verticalAlignment);
        data.overflow = static_cast<uint8_t>(comp.overflow);
        data.wordWrap = comp.wordWrap;
        data.lineSpacing = comp.lineSpacing;
        data.letterSpacing = comp.letterSpacing;
        data.richText = comp.richText;
        return data;
    }

    bool UIComponentService::setUILabelData(EntityHandle entity, const UILabelData& labelData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UILabelComponent>();
        comp.text = labelData.text;
        comp.fontRef = labelData.fontRef;
        comp.fontSize = labelData.fontSize;
        comp.fontStyle = static_cast<components::FontStyle>(labelData.fontStyle);
        comp.color = labelData.color;
        comp.horizontalAlignment = static_cast<components::HorizontalAlignment>(labelData.horizontalAlignment);
        comp.verticalAlignment = static_cast<components::VerticalAlignment>(labelData.verticalAlignment);
        comp.overflow = static_cast<components::TextOverflow>(labelData.overflow);
        comp.wordWrap = labelData.wordWrap;
        comp.lineSpacing = labelData.lineSpacing;
        comp.letterSpacing = labelData.letterSpacing;
        comp.richText = labelData.richText;
        return true;
    }

    std::optional<glm::vec2> UIComponentService::getUILabelPreferredSize(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            return std::nullopt;
        }


        return std::nullopt;
    }

    // ========== UI Button Operations ==========

    bool UIComponentService::addUIButtonComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIButtonComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIButtonComponent>();

        // Auto-add UIRectComponent if missing (button needs rect for layout/hit-testing)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        // Auto-add UIImageComponent if missing (button background)
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            sceneEntity.addComponent<components::UIImageComponent>();
        }

        // Auto-add UILabelComponent if missing (button text)
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            auto& labelComp = sceneEntity.addComponent<components::UILabelComponent>();
            labelComp.text = "Button";
            labelComp.horizontalAlignment = components::HorizontalAlignment::Center;
            labelComp.verticalAlignment = components::VerticalAlignment::Middle;
        }

        return true;
    }

    bool UIComponentService::removeUIButtonComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIButtonComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UIButtonComponent>();
        return true;
    }

    bool UIComponentService::hasUIButtonComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIButtonComponent>();
    }

    std::optional<UIButtonData> UIComponentService::getUIButtonData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIButtonComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIButtonComponent>();

        UIButtonData data;
        data.normalColor = comp.normalColor;
        data.hoveredColor = comp.hoveredColor;
        data.pressedColor = comp.pressedColor;
        data.disabledColor = comp.disabledColor;
        data.normalTextureRef = comp.normalTextureRef;
        data.hoverTextureRef = comp.hoverTextureRef;
        data.pressedTextureRef = comp.pressedTextureRef;
        data.disabledTextureRef = comp.disabledTextureRef;
        data.colorTransitionDuration = comp.colorTransitionDuration;
        data.interactable = comp.interactable;
        data.currentState = static_cast<uint8_t>(comp.currentState);
        return data;
    }

    bool UIComponentService::setUIButtonData(EntityHandle entity, const UIButtonData& buttonData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIButtonComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIButtonComponent>();
        comp.normalColor = buttonData.normalColor;
        comp.hoveredColor = buttonData.hoveredColor;
        comp.pressedColor = buttonData.pressedColor;
        comp.disabledColor = buttonData.disabledColor;
        comp.normalTextureRef = buttonData.normalTextureRef;
        comp.hoverTextureRef = buttonData.hoverTextureRef;
        comp.pressedTextureRef = buttonData.pressedTextureRef;
        comp.disabledTextureRef = buttonData.disabledTextureRef;
        comp.colorTransitionDuration = buttonData.colorTransitionDuration;
        comp.interactable = buttonData.interactable;
        comp.currentState = static_cast<components::UIButtonState>(buttonData.currentState);
        return true;
    }

    // ========== UI TextInput Operations ==========

    bool UIComponentService::addUITextInputComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UITextInputComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UITextInputComponent>();

        // Auto-add UIRectComponent if missing (text input needs rect for layout/hit-testing)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        // Auto-add UIImageComponent if missing (text input background)
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            sceneEntity.addComponent<components::UIImageComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUITextInputComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITextInputComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UITextInputComponent>();
        return true;
    }

    bool UIComponentService::hasUITextInputComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UITextInputComponent>();
    }

    std::optional<UITextInputData> UIComponentService::getUITextInputData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITextInputComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UITextInputComponent>();

        UITextInputData data;
        data.text = comp.text;
        data.placeholderText = comp.placeholderText;
        data.fontRef = comp.fontRef;
        data.fontSize = comp.fontSize;
        data.textColor = comp.textColor;
        data.placeholderColor = comp.placeholderColor;
        data.normalColor = comp.normalColor;
        data.hoveredColor = comp.hoveredColor;
        data.focusedColor = comp.focusedColor;
        data.disabledColor = comp.disabledColor;
        data.colorTransitionDuration = comp.colorTransitionDuration;
        data.interactable = comp.interactable;
        data.maxLength = comp.maxLength;
        data.selectionColor = comp.selectionColor;
        data.caretColor = comp.caretColor;
        data.caretWidth = comp.caretWidth;
        data.caretBlinkRate = comp.caretBlinkRate;
        data.currentState = static_cast<uint8_t>(comp.currentState);
        return data;
    }

    bool UIComponentService::setUITextInputData(EntityHandle entity, const UITextInputData& textInputData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITextInputComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UITextInputComponent>();
        comp.text = textInputData.text;
        comp.placeholderText = textInputData.placeholderText;
        comp.fontRef = textInputData.fontRef;
        comp.fontSize = textInputData.fontSize;
        comp.textColor = textInputData.textColor;
        comp.placeholderColor = textInputData.placeholderColor;
        comp.normalColor = textInputData.normalColor;
        comp.hoveredColor = textInputData.hoveredColor;
        comp.focusedColor = textInputData.focusedColor;
        comp.disabledColor = textInputData.disabledColor;
        comp.colorTransitionDuration = textInputData.colorTransitionDuration;
        comp.interactable = textInputData.interactable;
        comp.maxLength = textInputData.maxLength;
        comp.selectionColor = textInputData.selectionColor;
        comp.caretColor = textInputData.caretColor;
        comp.caretWidth = textInputData.caretWidth;
        comp.caretBlinkRate = textInputData.caretBlinkRate;
        comp.currentState = static_cast<components::UITextInputState>(textInputData.currentState);
        return true;
    }

    // ========== UI Checkbox Operations ==========

    bool UIComponentService::addUICheckboxComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UICheckboxComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UICheckboxComponent>();

        // Auto-add UIRectComponent if missing (checkbox needs rect for layout/hit-testing)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        // Auto-add UIImageComponent if missing (checkbox background)
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            sceneEntity.addComponent<components::UIImageComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUICheckboxComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UICheckboxComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UICheckboxComponent>();
        return true;
    }

    bool UIComponentService::hasUICheckboxComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UICheckboxComponent>();
    }

    std::optional<UICheckboxData> UIComponentService::getUICheckboxData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UICheckboxComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UICheckboxComponent>();

        UICheckboxData data;
        data.isChecked = comp.isChecked;
        data.groupName = comp.groupName;
        data.allowUncheck = comp.allowUncheck;
        data.uncheckedColor = comp.uncheckedColor;
        data.checkedColor = comp.checkedColor;
        data.hoveredColor = comp.hoveredColor;
        data.disabledColor = comp.disabledColor;
        data.uncheckedTextureRef = comp.uncheckedTextureRef;
        data.checkedTextureRef = comp.checkedTextureRef;
        data.hoveredTextureRef = comp.hoveredTextureRef;
        data.disabledTextureRef = comp.disabledTextureRef;
        data.colorTransitionDuration = comp.colorTransitionDuration;
        data.interactable = comp.interactable;
        data.labelToggle = comp.labelToggle;
        data.currentState = static_cast<uint8_t>(comp.currentState);
        return data;
    }

    bool UIComponentService::setUICheckboxData(EntityHandle entity, const UICheckboxData& checkboxData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UICheckboxComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UICheckboxComponent>();
        comp.isChecked = checkboxData.isChecked;
        comp.groupName = checkboxData.groupName;
        comp.allowUncheck = checkboxData.allowUncheck;
        comp.uncheckedColor = checkboxData.uncheckedColor;
        comp.checkedColor = checkboxData.checkedColor;
        comp.hoveredColor = checkboxData.hoveredColor;
        comp.disabledColor = checkboxData.disabledColor;
        comp.uncheckedTextureRef = checkboxData.uncheckedTextureRef;
        comp.checkedTextureRef = checkboxData.checkedTextureRef;
        comp.hoveredTextureRef = checkboxData.hoveredTextureRef;
        comp.disabledTextureRef = checkboxData.disabledTextureRef;
        comp.colorTransitionDuration = checkboxData.colorTransitionDuration;
        comp.interactable = checkboxData.interactable;
        comp.labelToggle = checkboxData.labelToggle;
        comp.currentState = static_cast<components::UICheckboxState>(checkboxData.currentState);
        return true;
    }

    // ========== Editor Viewport UI Picking ==========
    // In edit mode UI is rendered as world-space quads on the canvas entity's
    // world transform (UIFrameBuilder::prepareUIImagesWorldSpace), so picking
    // is a 3D ray test against each element's quad.

    namespace {

        // Entities with a UIRect but no visible content (bare layout rects)
        // are not pickable — mirrors UIInteractionSystem::computePointerOverUI.
        bool hasVisibleUIContent(entt::registry& registry, entt::entity entity) {
            return registry.any_of<components::UIImageComponent, components::UILabelComponent,
                                   components::UIButtonComponent, components::UICheckboxComponent,
                                   components::UITextInputComponent, components::UIDropdownComponent,
                                   components::UISliderComponent, components::UIProgressBarComponent>(entity);
        }

        // World-space corners (TL, TR, BR, BL) of the element's unit quad.
        std::optional<UIQuadCorners> computeUIWorldQuad(entt::registry& registry, entt::entity entity) {
            if (!registry.all_of<components::UIRectComponent>(entity)) {
                return std::nullopt;
            }

            auto canvasInfo = utilities::ui::findCanvasWithEntity(registry, entity);
            if (!canvasInfo.canvas || canvasInfo.canvasEntity == entt::null) {
                return std::nullopt;
            }
            if (!registry.all_of<components::WorldTransformComponent>(canvasInfo.canvasEntity)) {
                return std::nullopt;
            }

            const auto& worldTransform = registry.get<components::WorldTransformComponent>(canvasInfo.canvasEntity);
            const auto& rectComp = registry.get<components::UIRectComponent>(entity);

            glm::mat4 model = utilities::ui::computeCanvasImageModelMatrix(
                *canvasInfo.canvas, worldTransform.worldMatrix, rectComp);

            UIQuadCorners quad;
            utilities::ui::computeWorldQuadCorners(model, quad.corners);
            return quad;
        }

        std::optional<EntityHandle> pickUIEntityAt(const events::ui::PickUIEntityAtQuery& query) {
            if (query.viewportSize.x <= 0.0f || query.viewportSize.y <= 0.0f) {
                return std::nullopt;
            }

            // Screen position -> world ray (same math as ViewPortPicker::screenToWorldRay;
            // no extra Y-flip — the editor projection already applies the Vulkan flip).
            glm::vec2 screenPos = glm::clamp(query.screenPos, query.viewportPos,
                                             query.viewportPos + query.viewportSize);
            glm::vec2 normalized = (screenPos - query.viewportPos) / query.viewportSize;
            float ndcX = normalized.x * 2.0f - 1.0f;
            float ndcY = normalized.y * 2.0f - 1.0f;

            glm::mat4 invProj = glm::inverse(query.projMatrix);
            glm::mat4 invView = glm::inverse(query.viewMatrix);

            glm::vec4 nearPoint = invProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
            glm::vec4 farPoint = invProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            nearPoint /= nearPoint.w;
            farPoint /= farPoint.w;

            glm::vec3 worldNear = glm::vec3(invView * nearPoint);
            glm::vec3 worldFar = glm::vec3(invView * farPoint);
            math::Ray ray(worldNear, worldFar - worldNear);

            auto& registry = scene::EntityRegistry::getRegistry();

            // Nearest hit wins; smallest quad area breaks coplanar ties
            // (mirrors the runtime smallest-area widget hit-test).
            std::optional<EntityHandle> bestEntity;
            float bestT = std::numeric_limits<float>::max();
            float bestArea = std::numeric_limits<float>::max();

            auto rectView = registry.view<components::UIRectComponent>();
            for (auto entity : rectView) {
                if (!hasVisibleUIContent(registry, entity)) {
                    continue;
                }
                if (!scene::Entity::isEffectivelyActive(registry, entity)) {
                    continue;
                }

                auto quad = computeUIWorldQuad(registry, entity);
                if (!quad.has_value()) {
                    continue;
                }

                auto t = utilities::ui::intersectRayQuad(ray.origin, ray.direction, quad->corners);
                if (!t.has_value()) {
                    continue;
                }

                glm::vec3 u = quad->corners[1] - quad->corners[0];
                glm::vec3 v = quad->corners[3] - quad->corners[0];
                float area = glm::length(glm::cross(u, v));

                if (utilities::ui::isBetterQuadHit(bestEntity.has_value(), bestT, bestArea, *t, area)) {
                    bestT = *t;
                    bestArea = area;
                    bestEntity = internal::toHandle(entity);
                }
            }

            return bestEntity;
        }

        std::optional<UIQuadCorners> getUIEntityWorldQuad(EntityHandle entity) {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!internal::isValidHandle(entity, registry)) {
                return std::nullopt;
            }

            entt::entity enttEntity = internal::fromHandle(entity);
            if (!hasVisibleUIContent(registry, enttEntity)) {
                return std::nullopt;
            }
            return computeUIWorldQuad(registry, enttEntity);
        }
    }

    // ========== Event Handler Registration ==========

    void UIComponentService::registerInteractiveHandlers(events::EventDispatcher& dispatcher) {
        // Label commands
        dispatcher.registerCommandHandler<events::ui::AddUILabelComponentCommand>(
            [this](const events::ui::AddUILabelComponentCommand& cmd) {
                return addUILabelComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUILabelComponentCommand>(
            [this](const events::ui::RemoveUILabelComponentCommand& cmd) {
                return removeUILabelComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUILabelDataCommand>(
            [this](const events::ui::SetUILabelDataCommand& cmd) {
                return setUILabelData(cmd.entity, cmd.labelData);
            });

        dispatcher.registerQueryHandler<events::ui::HasUILabelComponentQuery>(
            [this](const events::ui::HasUILabelComponentQuery& query) {
                return hasUILabelComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUILabelDataQuery>(
            [this](const events::ui::GetUILabelDataQuery& query) {
                return getUILabelData(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUILabelPreferredSizeQuery>(
            [this](const events::ui::GetUILabelPreferredSizeQuery& query) {
                return getUILabelPreferredSize(query.entity);
            });

        // Button commands
        dispatcher.registerCommandHandler<events::ui::AddUIButtonComponentCommand>(
            [this](const events::ui::AddUIButtonComponentCommand& cmd) {
                return addUIButtonComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIButtonComponentCommand>(
            [this](const events::ui::RemoveUIButtonComponentCommand& cmd) {
                return removeUIButtonComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIButtonDataCommand>(
            [this](const events::ui::SetUIButtonDataCommand& cmd) {
                return setUIButtonData(cmd.entity, cmd.buttonData);
            });

        dispatcher.registerQueryHandler<events::ui::HasUIButtonComponentQuery>(
            [this](const events::ui::HasUIButtonComponentQuery& query) {
                return hasUIButtonComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIButtonDataQuery>(
            [this](const events::ui::GetUIButtonDataQuery& query) {
                return getUIButtonData(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::IsPointerOverUIQuery>(
            [](const events::ui::IsPointerOverUIQuery&) {
                auto& registry = scene::EntityRegistry::getRegistry();
                const auto* pointerState = registry.ctx().find<components::UIPointerState>();
                return pointerState != nullptr && pointerState->overUI;
            });

        // TextInput commands
        dispatcher.registerCommandHandler<events::ui::AddUITextInputComponentCommand>(
            [this](const events::ui::AddUITextInputComponentCommand& cmd) {
                return addUITextInputComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUITextInputComponentCommand>(
            [this](const events::ui::RemoveUITextInputComponentCommand& cmd) {
                return removeUITextInputComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUITextInputDataCommand>(
            [this](const events::ui::SetUITextInputDataCommand& cmd) {
                return setUITextInputData(cmd.entity, cmd.textInputData);
            });

        dispatcher.registerQueryHandler<events::ui::HasUITextInputComponentQuery>(
            [this](const events::ui::HasUITextInputComponentQuery& query) {
                return hasUITextInputComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUITextInputDataQuery>(
            [this](const events::ui::GetUITextInputDataQuery& query) {
                return getUITextInputData(query.entity);
            });

        // Checkbox commands
        dispatcher.registerCommandHandler<events::ui::AddUICheckboxComponentCommand>(
            [this](const events::ui::AddUICheckboxComponentCommand& cmd) {
                return addUICheckboxComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUICheckboxComponentCommand>(
            [this](const events::ui::RemoveUICheckboxComponentCommand& cmd) {
                return removeUICheckboxComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUICheckboxDataCommand>(
            [this](const events::ui::SetUICheckboxDataCommand& cmd) {
                return setUICheckboxData(cmd.entity, cmd.checkboxData);
            });

        dispatcher.registerQueryHandler<events::ui::HasUICheckboxComponentQuery>(
            [this](const events::ui::HasUICheckboxComponentQuery& query) {
                return hasUICheckboxComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUICheckboxDataQuery>(
            [this](const events::ui::GetUICheckboxDataQuery& query) {
                return getUICheckboxData(query.entity);
            });

        // Editor viewport UI picking (edit mode)
        dispatcher.registerQueryHandler<events::ui::PickUIEntityAtQuery>(
            [](const events::ui::PickUIEntityAtQuery& query) {
                return pickUIEntityAt(query);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIEntityWorldQuadQuery>(
            [](const events::ui::GetUIEntityWorldQuadQuery& query) {
                return getUIEntityWorldQuad(query.entity);
            });
    }

}
