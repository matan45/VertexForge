#include "UIInteractionSystem.hpp"
#include "UICommon.hpp"
#include "FramePreparationSystem.hpp"  // for FrameContext
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/UIEvents.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include <algorithm>
#include <limits>
#include <cctype>
#include <string>

namespace controllers::offscreen
{
    // GLFW key code constants (matching GLFW/glfw3.h)
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
            return comp.selectionStart >= 0 && comp.selectionEnd >= 0
                && comp.selectionStart != comp.selectionEnd;
        }

        bool deleteSelection(components::UITextInputComponent& comp)
        {
            if (comp.selectionStart < 0 || comp.selectionEnd < 0
                || comp.selectionStart == comp.selectionEnd)
                return false;

            int selMin = std::min(comp.selectionStart, comp.selectionEnd);
            int selMax = std::max(comp.selectionStart, comp.selectionEnd);
            selMin = std::max(0, std::min(selMin, static_cast<int>(comp.text.size())));
            selMax = std::max(0, std::min(selMax, static_cast<int>(comp.text.size())));

            comp.text.erase(selMin, selMax - selMin);
            comp.cursorPosition = selMin;
            comp.selectionStart = -1;
            comp.selectionEnd = -1;
            return true;
        }

        std::string getSelectedText(const components::UITextInputComponent& comp)
        {
            if (comp.selectionStart < 0 || comp.selectionEnd < 0
                || comp.selectionStart == comp.selectionEnd)
                return "";

            int selMin = std::min(comp.selectionStart, comp.selectionEnd);
            int selMax = std::max(comp.selectionStart, comp.selectionEnd);
            selMin = std::max(0, std::min(selMin, static_cast<int>(comp.text.size())));
            selMax = std::max(0, std::min(selMax, static_cast<int>(comp.text.size())));

            return comp.text.substr(selMin, selMax - selMin);
        }

        int findWordBoundaryLeft(const std::string& text, int pos)
        {
            if (pos <= 0) return 0;
            int p = pos - 1;
            // Skip non-alphanumeric
            while (p > 0 && !std::isalnum(static_cast<unsigned char>(text[p])))
                --p;
            // Skip alphanumeric
            while (p > 0 && std::isalnum(static_cast<unsigned char>(text[p - 1])))
                --p;
            return p;
        }

        int findWordBoundaryRight(const std::string& text, int pos)
        {
            int len = static_cast<int>(text.size());
            if (pos >= len) return len;
            int p = pos;
            // Skip alphanumeric
            while (p < len && std::isalnum(static_cast<unsigned char>(text[p])))
                ++p;
            // Skip non-alphanumeric
            while (p < len && !std::isalnum(static_cast<unsigned char>(text[p])))
                ++p;
            return p;
        }
    }

    // -----------------------------------------------------------------------
    // Orchestrator
    // -----------------------------------------------------------------------
    void UIInteractionSystem::processTextInputInteraction(const FrameContext& ctx)
    {
        if (!ctx.playModeActive)
            return;

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

    // -----------------------------------------------------------------------
    // PHASE 1: Hit test to find hovered text input (smallest-area wins)
    // -----------------------------------------------------------------------
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

            if (!comp.interactable)
                continue;

            if (registry.all_of<components::NameComponent>(entity))
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;

            const auto* canvas = ui_common::findCanvasForEntity(registry, entity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                canvas = &registry.get<components::UICanvasComponent>(entity);
            if (!canvas)
                continue;

            float scale = ui_common::computeCanvasScale(canvas, vw, vh);

            const auto& rectComp = registry.get<components::UIRectComponent>(entity);
            ui_common::PixelRect rect = ui_common::resolvePixelRect(rectComp, vw, vh, scale);

            auto [scrollAncestor, scissor] = ui_common::findScrollInfo(registry, entity, scrollContainers);
            ui_common::applyScrollOffset(rect, scrollAncestor, scrollContainers);

            if (ui_common::hitTestRect(ctx.mousePosition, rect, scissor))
            {
                float area = rect.w * rect.h;
                if (area < smallestArea)
                {
                    smallestArea = area;
                    hoveredTextInput = entity;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // PHASE 2: Focus management (click focus/unfocus, double-click, escape)
    // -----------------------------------------------------------------------
    void UIInteractionSystem::textInputFocusManagement(const FrameContext& ctx, entt::entity hoveredTextInput)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = events::EventDispatcher::instance();

        if (ctx.leftMousePressed)
        {
            if (hoveredTextInput != entt::null)
            {
                // Click on a text input -> focus it
                if (focusedTextInput != hoveredTextInput)
                {
                    // Unfocus previous
                    if (focusedTextInput != entt::null && registry.valid(focusedTextInput)
                        && registry.all_of<components::UITextInputComponent>(focusedTextInput))
                    {
                        auto& prevComp = registry.get<components::UITextInputComponent>(focusedTextInput);
                        prevComp.currentState = components::UITextInputState::Normal;
                        prevComp.selectionStart = -1;
                        prevComp.selectionEnd = -1;

                        auto [handle, name] = ui_common::makeEntityPayload(registry, focusedTextInput);
                        events::ui::UITextInputUnfocusedNotification notif;
                        notif.entity = handle;
                        notif.entityName = std::move(name);
                        dispatcher.publish(notif);
                    }

                    focusedTextInput = hoveredTextInput;
                    auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
                    comp.currentState = components::UITextInputState::Focused;
                    comp.cursorPosition = static_cast<int>(comp.text.size());
                    comp.caretBlinkTimer = 0.0f;
                    comp.caretVisible = true;
                    comp.selectionStart = -1;
                    comp.selectionEnd = -1;

                    auto [handle, name] = ui_common::makeEntityPayload(registry, focusedTextInput);
                    events::ui::UITextInputFocusedNotification notif;
                    notif.entity = handle;
                    notif.entityName = std::move(name);
                    dispatcher.publish(notif);
                }
            }
            else
            {
                // Click outside -> unfocus
                if (focusedTextInput != entt::null && registry.valid(focusedTextInput)
                    && registry.all_of<components::UITextInputComponent>(focusedTextInput))
                {
                    auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
                    comp.currentState = components::UITextInputState::Normal;
                    comp.selectionStart = -1;
                    comp.selectionEnd = -1;

                    auto [handle, name] = ui_common::makeEntityPayload(registry, focusedTextInput);
                    events::ui::UITextInputUnfocusedNotification notif;
                    notif.entity = handle;
                    notif.entityName = std::move(name);
                    dispatcher.publish(notif);
                }
                focusedTextInput = entt::null;
            }
        }

        // Double-click on focused text input -> select word
        if (ctx.leftMouseDoubleClick && focusedTextInput != entt::null
            && hoveredTextInput == focusedTextInput
            && registry.valid(focusedTextInput)
            && registry.all_of<components::UITextInputComponent>(focusedTextInput))
        {
            auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
            if (!comp.text.empty())
            {
                int pos = comp.cursorPosition;
                comp.selectionStart = findWordBoundaryLeft(comp.text, pos);
                comp.selectionEnd = findWordBoundaryRight(comp.text, pos);
                comp.cursorPosition = comp.selectionEnd;
            }
        }

        // Escape -> unfocus
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Escape) && focusedTextInput != entt::null)
        {
            if (registry.valid(focusedTextInput)
                && registry.all_of<components::UITextInputComponent>(focusedTextInput))
            {
                auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
                comp.currentState = components::UITextInputState::Normal;
                comp.selectionStart = -1;
                comp.selectionEnd = -1;

                auto [handle, name] = ui_common::makeEntityPayload(registry, focusedTextInput);
                events::ui::UITextInputUnfocusedNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }
            focusedTextInput = entt::null;
        }
    }

    // -----------------------------------------------------------------------
    // PHASE 3: Text editing (character input, keyboard shortcuts, clipboard)
    // -----------------------------------------------------------------------
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
        int textLen = static_cast<int>(comp.text.size());

        // Character input
        for (uint32_t codepoint : ctx.charInput)
        {
            // Skip control characters
            if (codepoint < 32 || codepoint == 127)
                continue;

            // Delete selection first if any
            deleteSelection(comp);
            textLen = static_cast<int>(comp.text.size());

            // Check max length
            if (comp.maxLength > 0 && textLen >= comp.maxLength)
                continue;

            // Insert character (ASCII only for simplicity -- handles most use cases)
            if (codepoint < 128)
            {
                comp.text.insert(comp.cursorPosition, 1, static_cast<char>(codepoint));
            }
            else
            {
                // UTF-8 encode
                std::string utf8;
                if (codepoint < 0x80)
                {
                    utf8 += static_cast<char>(codepoint);
                }
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
            textChanged = true;
            comp.caretBlinkTimer = 0.0f;
            comp.caretVisible = true;
        }
        textLen = static_cast<int>(comp.text.size());

        // Backspace
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Backspace))
        {
            if (hasSelection(comp))
            {
                deleteSelection(comp);
                textChanged = true;
            }
            else if (comp.cursorPosition > 0)
            {
                if (ctrlDown)
                {
                    int newPos = findWordBoundaryLeft(comp.text, comp.cursorPosition);
                    comp.text.erase(newPos, comp.cursorPosition - newPos);
                    comp.cursorPosition = newPos;
                }
                else
                {
                    comp.text.erase(comp.cursorPosition - 1, 1);
                    comp.cursorPosition--;
                }
                textChanged = true;
            }
            comp.caretBlinkTimer = 0.0f;
            comp.caretVisible = true;
        }

        // Delete
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Delete))
        {
            textLen = static_cast<int>(comp.text.size());
            if (hasSelection(comp))
            {
                deleteSelection(comp);
                textChanged = true;
            }
            else if (comp.cursorPosition < textLen)
            {
                if (ctrlDown)
                {
                    int newPos = findWordBoundaryRight(comp.text, comp.cursorPosition);
                    comp.text.erase(comp.cursorPosition, newPos - comp.cursorPosition);
                }
                else
                {
                    comp.text.erase(comp.cursorPosition, 1);
                }
                textChanged = true;
            }
            comp.caretBlinkTimer = 0.0f;
            comp.caretVisible = true;
        }

        // Left arrow
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Left))
        {
            textLen = static_cast<int>(comp.text.size());
            int prevPos = comp.cursorPosition;

            if (ctrlDown)
                comp.cursorPosition = findWordBoundaryLeft(comp.text, comp.cursorPosition);
            else if (comp.cursorPosition > 0)
                comp.cursorPosition--;

            if (shiftDown)
            {
                if (comp.selectionStart < 0) comp.selectionStart = prevPos;
                comp.selectionEnd = comp.cursorPosition;
            }
            else
            {
                if (hasSelection(comp))
                    comp.cursorPosition = std::min(comp.selectionStart, comp.selectionEnd);
                comp.selectionStart = -1;
                comp.selectionEnd = -1;
            }
            comp.caretBlinkTimer = 0.0f;
            comp.caretVisible = true;
        }

        // Right arrow
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Right))
        {
            textLen = static_cast<int>(comp.text.size());
            int prevPos = comp.cursorPosition;

            if (ctrlDown)
                comp.cursorPosition = findWordBoundaryRight(comp.text, comp.cursorPosition);
            else if (comp.cursorPosition < textLen)
                comp.cursorPosition++;

            if (shiftDown)
            {
                if (comp.selectionStart < 0) comp.selectionStart = prevPos;
                comp.selectionEnd = comp.cursorPosition;
            }
            else
            {
                if (hasSelection(comp))
                    comp.cursorPosition = std::max(comp.selectionStart, comp.selectionEnd);
                comp.selectionStart = -1;
                comp.selectionEnd = -1;
            }
            comp.caretBlinkTimer = 0.0f;
            comp.caretVisible = true;
        }

        // Home
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Home))
        {
            int prevPos = comp.cursorPosition;
            comp.cursorPosition = 0;

            if (shiftDown)
            {
                if (comp.selectionStart < 0) comp.selectionStart = prevPos;
                comp.selectionEnd = 0;
            }
            else
            {
                comp.selectionStart = -1;
                comp.selectionEnd = -1;
            }
            comp.caretBlinkTimer = 0.0f;
            comp.caretVisible = true;
        }

        // End
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::End))
        {
            textLen = static_cast<int>(comp.text.size());
            int prevPos = comp.cursorPosition;
            comp.cursorPosition = textLen;

            if (shiftDown)
            {
                if (comp.selectionStart < 0) comp.selectionStart = prevPos;
                comp.selectionEnd = textLen;
            }
            else
            {
                comp.selectionStart = -1;
                comp.selectionEnd = -1;
            }
            comp.caretBlinkTimer = 0.0f;
            comp.caretVisible = true;
        }

        // Ctrl+A -> select all
        if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::A))
        {
            textLen = static_cast<int>(comp.text.size());
            comp.selectionStart = 0;
            comp.selectionEnd = textLen;
            comp.cursorPosition = textLen;
        }

        // Ctrl+C -> copy
        if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::C))
        {
            if (hasSelection(comp) && ctx.setClipboardText)
            {
                ctx.setClipboardText(getSelectedText(comp));
            }
        }

        // Ctrl+X -> cut
        if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::X))
        {
            if (hasSelection(comp) && ctx.setClipboardText)
            {
                ctx.setClipboardText(getSelectedText(comp));
                deleteSelection(comp);
                textChanged = true;
            }
        }

        // Ctrl+V -> paste
        if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::V))
        {
            if (ctx.getClipboardText)
            {
                std::string clipboard = ctx.getClipboardText();
                if (!clipboard.empty())
                {
                    deleteSelection(comp);
                    textLen = static_cast<int>(comp.text.size());

                    // Enforce max length
                    if (comp.maxLength > 0)
                    {
                        int remaining = comp.maxLength - textLen;
                        if (remaining <= 0)
                            clipboard.clear();
                        else if (static_cast<int>(clipboard.size()) > remaining)
                            clipboard = clipboard.substr(0, remaining);
                    }

                    if (!clipboard.empty())
                    {
                        comp.text.insert(comp.cursorPosition, clipboard);
                        comp.cursorPosition += static_cast<int>(clipboard.size());
                        textChanged = true;
                    }
                }
            }
            comp.caretBlinkTimer = 0.0f;
            comp.caretVisible = true;
        }

        // Enter -> submit
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Enter))
        {
            auto& dispatcher = events::EventDispatcher::instance();
            auto [handle, name] = ui_common::makeEntityPayload(registry, focusedTextInput);
            events::ui::UITextInputSubmitNotification notif;
            notif.entity = handle;
            notif.entityName = std::move(name);
            notif.text = comp.text;
            dispatcher.publish(notif);
        }

        // Publish text changed notification
        if (textChanged)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            auto [handle, name] = ui_common::makeEntityPayload(registry, focusedTextInput);
            events::ui::UITextInputChangedNotification notif;
            notif.entity = handle;
            notif.entityName = std::move(name);
            notif.text = comp.text;
            dispatcher.publish(notif);
        }

        // Clamp cursor position
        comp.cursorPosition = std::max(0, std::min(comp.cursorPosition, static_cast<int>(comp.text.size())));
    }

    // -----------------------------------------------------------------------
    // PHASE 4: State machine transitions + caret blink + color lerp + visual override
    // -----------------------------------------------------------------------
    void UIInteractionSystem::textInputStateAndVisuals(const FrameContext& ctx, entt::entity hoveredTextInput)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto textInputView = registry.view<components::UITextInputComponent, components::UIRectComponent>();

        for (auto entity : textInputView)
        {
            auto& comp = registry.get<components::UITextInputComponent>(entity);

            if (registry.all_of<components::NameComponent>(entity))
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;

            // State machine
            if (!comp.interactable)
            {
                comp.currentState = components::UITextInputState::Disabled;
            }
            else if (entity == focusedTextInput)
            {
                comp.currentState = components::UITextInputState::Focused;
            }
            else if (entity == hoveredTextInput)
            {
                comp.currentState = components::UITextInputState::Hovered;
            }
            else
            {
                comp.currentState = components::UITextInputState::Normal;
            }

            // Caret blink timer (only when focused)
            if (comp.currentState == components::UITextInputState::Focused && comp.caretBlinkRate > 0.0f)
            {
                comp.caretBlinkTimer += ctx.deltaTime;
                if (comp.caretBlinkTimer >= comp.caretBlinkRate)
                {
                    comp.caretBlinkTimer -= comp.caretBlinkRate;
                    comp.caretVisible = !comp.caretVisible;
                }
            }
            else
            {
                comp.caretVisible = false;
            }

            // Color lerp
            glm::vec4 targetColor;
            switch (comp.currentState)
            {
            case components::UITextInputState::Hovered:
                targetColor = comp.hoveredColor;
                break;
            case components::UITextInputState::Focused:
                targetColor = comp.focusedColor;
                break;
            case components::UITextInputState::Disabled:
                targetColor = comp.disabledColor;
                break;
            default:
                targetColor = comp.normalColor;
                break;
            }

            if (comp.colorTransitionDuration > 0.0f && ctx.deltaTime > 0.0f)
            {
                float t = std::min(1.0f, ctx.deltaTime / comp.colorTransitionDuration);
                comp.currentDisplayColor = glm::mix(comp.currentDisplayColor, targetColor, t);
            }
            else
            {
                comp.currentDisplayColor = targetColor;
            }

            // Override UIImageComponent color tint
            if (registry.all_of<components::UIImageComponent>(entity))
            {
                auto& imageComp = registry.get<components::UIImageComponent>(entity);
                imageComp.colorTint = comp.currentDisplayColor;
            }
        }
    }
}
