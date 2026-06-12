#include "UIInteractionSystem.hpp"
#include "UICommon.hpp"
#include "FramePreparationSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIEvents.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include <algorithm>
#include <limits>

namespace controllers::offscreen
{
    namespace keycode
    {
        constexpr int Backspace = 259;
        constexpr int Delete = 261;
        constexpr int Right = 262;
        constexpr int Left = 263;
        constexpr int Home = 268;
        constexpr int End = 269;
        constexpr int Enter = 257;
        constexpr int Escape = 256;
        constexpr int Tab = 258;
        constexpr int A = 65;
        constexpr int C = 67;
        constexpr int V = 86;
        constexpr int X = 88;
        constexpr int LeftControl = 341;
        constexpr int RightControl = 345;
        constexpr int LeftShift = 340;
        constexpr int RightShift = 344;
    }

    namespace
    {
        bool hasSelection(const components::UITextInputComponent& comp)
        {
            return comp.selectionStart >= 0 && comp.selectionEnd >= 0 && comp.selectionStart != comp.selectionEnd;
        }

        bool deleteSelection(components::UITextInputComponent& comp)
        {
            if (comp.selectionStart < 0 || comp.selectionEnd < 0 || comp.selectionStart == comp.selectionEnd)
                return false;

            int selMin = std::max(0, std::min({comp.selectionStart, comp.selectionEnd, static_cast<int>(comp.text.size())}));
            int selMax = std::max(0, std::min(std::max(comp.selectionStart, comp.selectionEnd), static_cast<int>(comp.text.size())));

            comp.text.erase(selMin, selMax - selMin);
            comp.cursorPosition = selMin;
            comp.selectionStart = -1;
            comp.selectionEnd = -1;
            return true;
        }

        std::string getSelectedText(const components::UITextInputComponent& comp)
        {
            if (!hasSelection(comp)) return "";
            int selMin = std::max(0, std::min({comp.selectionStart, comp.selectionEnd, static_cast<int>(comp.text.size())}));
            int selMax = std::max(0, std::min(std::max(comp.selectionStart, comp.selectionEnd), static_cast<int>(comp.text.size())));
            return comp.text.substr(selMin, selMax - selMin);
        }

        int findWordBoundaryLeft(const std::string& text, int pos)
        {
            if (pos <= 0) return 0;
            int p = pos - 1;
            while (p > 0 && !std::isalnum(static_cast<unsigned char>(text[p]))) --p;
            while (p > 0 && std::isalnum(static_cast<unsigned char>(text[p - 1]))) --p;
            return p;
        }

        int findWordBoundaryRight(const std::string& text, int pos)
        {
            int len = static_cast<int>(text.size());
            if (pos >= len) return len;
            int p = pos;
            while (p < len && std::isalnum(static_cast<unsigned char>(text[p]))) ++p;
            while (p < len && !std::isalnum(static_cast<unsigned char>(text[p]))) ++p;
            return p;
        }
    }

    void UIInteractionSystem::processTextInputInteraction(const FrameContext& ctx)
    {
        if (!ctx.playModeActive) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto textInputView = registry.view<components::UITextInputComponent, components::UIRectComponent>();

        if (textInputView.size_hint() == 0)
        {
            focusedTextInput = entt::null;
            return;
        }

        entt::entity hoveredTextInput = entt::null;
        textInputHitTest(ctx, hoveredTextInput);
        textInputFocusManagement(ctx, hoveredTextInput);
        textInputEditing(ctx);
        textInputStateAndVisuals(ctx, hoveredTextInput);
    }

    void UIInteractionSystem::textInputHitTest(const FrameContext& ctx, entt::entity& hoveredTextInput)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        auto scrollContainers = ui_common::buildScrollContainerMap(registry, vw, vh);
        auto textInputView = registry.view<components::UITextInputComponent, components::UIRectComponent>();
        float smallestArea = std::numeric_limits<float>::max();

        for (auto entity : textInputView)
        {
            auto& comp = registry.get<components::UITextInputComponent>(entity);
            if (!comp.interactable) continue;
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
            if (!ui_common::isInteractionAllowed(registry, entity)) continue;

            const auto* canvas = ui_common::findCanvasForEntity(registry, entity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                canvas = &registry.get<components::UICanvasComponent>(entity);
            if (!canvas) continue;

            float scale = ui_common::computeCanvasScale(canvas, vw, vh);
            const auto& rectComp = registry.get<components::UIRectComponent>(entity);
            ui_common::PixelRect rect = ui_common::resolvePixelRect(rectComp, vw, vh, scale);

            auto [scrollAncestor, scissor] = ui_common::findScrollInfo(registry, entity, scrollContainers);
            ui_common::applyScrollOffset(rect, scrollAncestor, scrollContainers);

            if (ui_common::hitTestRect(ctx.mousePosition, rect, scissor))
            {
                float area = rect.w * rect.h;
                if (area < smallestArea) { smallestArea = area; hoveredTextInput = entity; }
            }
        }
    }

    void UIInteractionSystem::textInputFocusManagement(const FrameContext& ctx, entt::entity hoveredTextInput)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = events::EventDispatcher::instance();

        auto unfocusCurrent = [&]() {
            if (focusedTextInput != entt::null && registry.valid(focusedTextInput)
                && registry.all_of<components::UITextInputComponent>(focusedTextInput))
            {
                auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
                comp.currentState = components::UITextInputState::Normal;
                comp.selectionStart = -1; comp.selectionEnd = -1;

                auto [handle, name] = ui_common::makeEntityPayload(registry, focusedTextInput);
                events::ui::UITextInputUnfocusedNotification notif;
                notif.entity = handle; notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }
        };

        if (ctx.leftMousePressed)
        {
            if (hoveredTextInput != entt::null)
            {
                if (focusedTextInput != hoveredTextInput)
                {
                    unfocusCurrent();
                    focusedTextInput = hoveredTextInput;
                    auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
                    comp.currentState = components::UITextInputState::Focused;
                    comp.cursorPosition = static_cast<int>(comp.text.size());
                    comp.caretBlinkTimer = 0.0f; comp.caretVisible = true;
                    comp.selectionStart = -1; comp.selectionEnd = -1;

                    auto [handle, name] = ui_common::makeEntityPayload(registry, focusedTextInput);
                    events::ui::UITextInputFocusedNotification notif;
                    notif.entity = handle; notif.entityName = std::move(name);
                    dispatcher.publish(notif);
                }
            }
            else
            {
                unfocusCurrent();
                focusedTextInput = entt::null;
            }
        }

        if (ctx.leftMouseDoubleClick && focusedTextInput != entt::null
            && hoveredTextInput == focusedTextInput
            && registry.valid(focusedTextInput)
            && registry.all_of<components::UITextInputComponent>(focusedTextInput))
        {
            auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
            if (!comp.text.empty())
            {
                comp.selectionStart = findWordBoundaryLeft(comp.text, comp.cursorPosition);
                comp.selectionEnd = findWordBoundaryRight(comp.text, comp.cursorPosition);
                comp.cursorPosition = comp.selectionEnd;
            }
        }

        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Escape) && focusedTextInput != entt::null)
        {
            unfocusCurrent();
            focusedTextInput = entt::null;
        }
    }
}
