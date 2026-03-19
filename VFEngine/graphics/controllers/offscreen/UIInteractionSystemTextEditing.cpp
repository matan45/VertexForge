#include "UIInteractionSystem.hpp"
#include "UICommon.hpp"
#include "FramePreparationSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIEvents.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include <algorithm>
#include <cctype>

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
            if (!hasSelection(comp)) return false;
            int selMin = std::max(0, std::min({comp.selectionStart, comp.selectionEnd, static_cast<int>(comp.text.size())}));
            int selMax = std::max(0, std::min(std::max(comp.selectionStart, comp.selectionEnd), static_cast<int>(comp.text.size())));
            comp.text.erase(selMin, selMax - selMin);
            comp.cursorPosition = selMin;
            comp.selectionStart = -1; comp.selectionEnd = -1;
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

        void insertCodepoint(components::UITextInputComponent& comp, uint32_t codepoint)
        {
            if (codepoint < 128)
            {
                comp.text.insert(comp.cursorPosition, 1, static_cast<char>(codepoint));
            }
            else
            {
                std::string utf8;
                if (codepoint < 0x80)
                    utf8 += static_cast<char>(codepoint);
                else if (codepoint < 0x800)
                {
                    utf8 += static_cast<char>(0xC0 | (codepoint >> 6));
                    utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
                }
                else if (codepoint < 0x10000)
                {
                    utf8 += static_cast<char>(0xE0 | (codepoint >> 12));
                    utf8 += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
                }
                else
                {
                    utf8 += static_cast<char>(0xF0 | (codepoint >> 18));
                    utf8 += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                    utf8 += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
                }
                comp.text.insert(comp.cursorPosition, utf8);
            }
            comp.cursorPosition++;
        }

        void handleArrowKey(components::UITextInputComponent& comp, bool isLeft, bool ctrlDown, bool shiftDown)
        {
            int prevPos = comp.cursorPosition;
            int textLen = static_cast<int>(comp.text.size());

            if (isLeft)
            {
                if (ctrlDown) comp.cursorPosition = findWordBoundaryLeft(comp.text, comp.cursorPosition);
                else if (comp.cursorPosition > 0) comp.cursorPosition--;
            }
            else
            {
                if (ctrlDown) comp.cursorPosition = findWordBoundaryRight(comp.text, comp.cursorPosition);
                else if (comp.cursorPosition < textLen) comp.cursorPosition++;
            }

            if (shiftDown)
            {
                if (comp.selectionStart < 0) comp.selectionStart = prevPos;
                comp.selectionEnd = comp.cursorPosition;
            }
            else
            {
                if (hasSelection(comp))
                    comp.cursorPosition = isLeft ? std::min(comp.selectionStart, comp.selectionEnd) : std::max(comp.selectionStart, comp.selectionEnd);
                comp.selectionStart = -1; comp.selectionEnd = -1;
            }
            comp.caretBlinkTimer = 0.0f; comp.caretVisible = true;
        }

        void handleHomeEnd(components::UITextInputComponent& comp, bool isHome, bool shiftDown)
        {
            int prevPos = comp.cursorPosition;
            comp.cursorPosition = isHome ? 0 : static_cast<int>(comp.text.size());

            if (shiftDown)
            {
                if (comp.selectionStart < 0) comp.selectionStart = prevPos;
                comp.selectionEnd = comp.cursorPosition;
            }
            else
            {
                comp.selectionStart = -1; comp.selectionEnd = -1;
            }
            comp.caretBlinkTimer = 0.0f; comp.caretVisible = true;
        }
    }

    void UIInteractionSystem::textInputEditing(const FrameContext& ctx)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (focusedTextInput == entt::null || !registry.valid(focusedTextInput)
            || !registry.all_of<components::UITextInputComponent>(focusedTextInput))
            return;

        bool ctrlDown = ctx.isKeyDown && (ctx.isKeyDown(keycode::LeftControl) || ctx.isKeyDown(keycode::RightControl));
        bool shiftDown = ctx.isKeyDown && (ctx.isKeyDown(keycode::LeftShift) || ctx.isKeyDown(keycode::RightShift));

        auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
        bool textChanged = false;

        for (uint32_t codepoint : ctx.charInput)
        {
            if (codepoint < 32 || codepoint == 127) continue;
            deleteSelection(comp);
            if (comp.maxLength > 0 && static_cast<int>(comp.text.size()) >= comp.maxLength) continue;
            insertCodepoint(comp, codepoint);
            textChanged = true;
            comp.caretBlinkTimer = 0.0f; comp.caretVisible = true;
        }

        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Backspace))
        {
            if (hasSelection(comp)) { deleteSelection(comp); textChanged = true; }
            else if (comp.cursorPosition > 0)
            {
                if (ctrlDown) { int np = findWordBoundaryLeft(comp.text, comp.cursorPosition); comp.text.erase(np, comp.cursorPosition - np); comp.cursorPosition = np; }
                else { comp.text.erase(comp.cursorPosition - 1, 1); comp.cursorPosition--; }
                textChanged = true;
            }
            comp.caretBlinkTimer = 0.0f; comp.caretVisible = true;
        }

        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Delete))
        {
            int textLen = static_cast<int>(comp.text.size());
            if (hasSelection(comp)) { deleteSelection(comp); textChanged = true; }
            else if (comp.cursorPosition < textLen)
            {
                if (ctrlDown) { int np = findWordBoundaryRight(comp.text, comp.cursorPosition); comp.text.erase(comp.cursorPosition, np - comp.cursorPosition); }
                else comp.text.erase(comp.cursorPosition, 1);
                textChanged = true;
            }
            comp.caretBlinkTimer = 0.0f; comp.caretVisible = true;
        }

        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Left)) handleArrowKey(comp, true, ctrlDown, shiftDown);
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Right)) handleArrowKey(comp, false, ctrlDown, shiftDown);
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Home)) handleHomeEnd(comp, true, shiftDown);
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::End)) handleHomeEnd(comp, false, shiftDown);

        if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::A))
        { int tl = static_cast<int>(comp.text.size()); comp.selectionStart = 0; comp.selectionEnd = tl; comp.cursorPosition = tl; }

        if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::C) && hasSelection(comp) && ctx.setClipboardText)
            ctx.setClipboardText(getSelectedText(comp));

        if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::X) && hasSelection(comp) && ctx.setClipboardText)
        { ctx.setClipboardText(getSelectedText(comp)); deleteSelection(comp); textChanged = true; }

        if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::V) && ctx.getClipboardText)
        {
            std::string clipboard = ctx.getClipboardText();
            if (!clipboard.empty())
            {
                deleteSelection(comp);
                int textLen = static_cast<int>(comp.text.size());
                if (comp.maxLength > 0) { int remaining = comp.maxLength - textLen; if (remaining <= 0) clipboard.clear(); else if (static_cast<int>(clipboard.size()) > remaining) clipboard = clipboard.substr(0, remaining); }
                if (!clipboard.empty()) { comp.text.insert(comp.cursorPosition, clipboard); comp.cursorPosition += static_cast<int>(clipboard.size()); textChanged = true; }
            }
            comp.caretBlinkTimer = 0.0f; comp.caretVisible = true;
        }

        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Enter))
        {
            auto& dispatcher = events::EventDispatcher::instance();
            auto [handle, name] = ui_common::makeEntityPayload(registry, focusedTextInput);
            events::ui::UITextInputSubmitNotification notif;
            notif.entity = handle; notif.entityName = std::move(name); notif.text = comp.text;
            dispatcher.publish(notif);
        }

        if (textChanged)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            auto [handle, name] = ui_common::makeEntityPayload(registry, focusedTextInput);
            events::ui::UITextInputChangedNotification notif;
            notif.entity = handle; notif.entityName = std::move(name); notif.text = comp.text;
            dispatcher.publish(notif);
        }

        comp.cursorPosition = std::max(0, std::min(comp.cursorPosition, static_cast<int>(comp.text.size())));
    }

    void UIInteractionSystem::textInputStateAndVisuals(const FrameContext& ctx, entt::entity hoveredTextInput)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto textInputView = registry.view<components::UITextInputComponent, components::UIRectComponent>();

        for (auto entity : textInputView)
        {
            auto& comp = registry.get<components::UITextInputComponent>(entity);
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;

            if (!comp.interactable) comp.currentState = components::UITextInputState::Disabled;
            else if (entity == focusedTextInput) comp.currentState = components::UITextInputState::Focused;
            else if (entity == hoveredTextInput) comp.currentState = components::UITextInputState::Hovered;
            else comp.currentState = components::UITextInputState::Normal;

            if (comp.currentState == components::UITextInputState::Focused && comp.caretBlinkRate > 0.0f)
            {
                comp.caretBlinkTimer += ctx.deltaTime;
                if (comp.caretBlinkTimer >= comp.caretBlinkRate) { comp.caretBlinkTimer -= comp.caretBlinkRate; comp.caretVisible = !comp.caretVisible; }
            }
            else
            {
                comp.caretVisible = false;
            }

            glm::vec4 targetColor;
            switch (comp.currentState)
            {
            case components::UITextInputState::Hovered:  targetColor = comp.hoveredColor; break;
            case components::UITextInputState::Focused:  targetColor = comp.focusedColor; break;
            case components::UITextInputState::Disabled: targetColor = comp.disabledColor; break;
            default: targetColor = comp.normalColor; break;
            }

            if (comp.colorTransitionDuration > 0.0f && ctx.deltaTime > 0.0f)
                comp.currentDisplayColor = glm::mix(comp.currentDisplayColor, targetColor, std::min(1.0f, ctx.deltaTime / comp.colorTransitionDuration));
            else
                comp.currentDisplayColor = targetColor;

            if (registry.all_of<components::UIImageComponent>(entity))
                registry.get<components::UIImageComponent>(entity).colorTint = comp.currentDisplayColor;
        }
    }
}
