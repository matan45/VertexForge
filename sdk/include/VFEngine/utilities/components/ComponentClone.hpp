#pragma once
#include "Components.hpp"
#include "../scene/Entity.hpp"

#include <entt/entt.hpp>
#include <type_traits>
#include <utility>

// Centralized entity-component cloning for VK-1343.
//
// Duplicating an entity used to hand-copy a fixed component list, silently dropping UI and
// other registered components. This facility folds over components::OptionalComponents (the
// single component inventory) and value-copies every present component, resetting runtime-only
// fields to match serialization deserialize semantics.
//
// ScriptComponent is intentionally excluded - the caller reattaches scripts through the
// scripting system so runtime script instances are created. Dispatcher-dependent side-effects
// (e.g. the MeshComponent GPU-registration notification) also stay in the caller, since this
// header lives in Utilities and must not depend on the services event layer.
namespace components
{
    // Customization point: reset runtime-only fields after a value-copy. Default is a no-op for
    // components that hold only authored config. Specializations below mirror the corresponding
    // deserialize functions in utilities/serialization/SceneSerializeUI*.cpp.
    template <typename T>
    inline void resetClonedRuntimeState(T&)
    {
    }

    template <>
    inline void resetClonedRuntimeState<CameraComponent>(CameraComponent& c)
    {
        c.isPrimary = false; // never duplicate primary-camera status
        c.updateProjectionMatrix();
    }

    template <>
    inline void resetClonedRuntimeState<UIImageComponent>(UIImageComponent& c)
    {
        c.renderTextureSource = entt::null; // keep renderTextureSourceName for later resolution
    }

    template <>
    inline void resetClonedRuntimeState<UIScrollComponent>(UIScrollComponent& c)
    {
        c.scrollOffset = {0.0f, 0.0f};
        c.contentSize = {0.0f, 0.0f};
        c.viewportSize = {0.0f, 0.0f};
        c.computedScissorRect = {0.0f, 0.0f, 0.0f, 0.0f};
        c.isDragging = false;
        c.dragAxis = 0;
        c.dragStartScrollOffset = {0.0f, 0.0f};
        c.dragStartMousePos = {0.0f, 0.0f};
    }

    template <>
    inline void resetClonedRuntimeState<UIButtonComponent>(UIButtonComponent& c)
    {
        c.currentState = UIButtonState::Normal;
        c.currentDisplayColor = c.normalColor;
    }

    template <>
    inline void resetClonedRuntimeState<UITextInputComponent>(UITextInputComponent& c)
    {
        c.currentState = UITextInputState::Normal;
        c.cursorPosition = 0;
        c.selectionStart = -1;
        c.selectionEnd = -1;
        c.caretBlinkTimer = 0.0f;
        c.caretVisible = true;
        c.currentDisplayColor = c.normalColor;
        c.scrollOffsetX = 0.0f;
    }

    template <>
    inline void resetClonedRuntimeState<UICheckboxComponent>(UICheckboxComponent& c)
    {
        c.currentState = UICheckboxState::Normal;
        c.currentDisplayColor = c.isChecked ? c.checkedColor : c.uncheckedColor;
    }

    template <>
    inline void resetClonedRuntimeState<UIDropdownComponent>(UIDropdownComponent& c)
    {
        c.currentState = UIDropdownState::Normal;
        c.isOpen = false;
        c.hoveredOptionIndex = -1;
        c.currentDisplayColor = c.normalColor;
        c.listScrollOffset = 0.0f;
        // activeDropdownEntity is a class static, not part of the object value - leave it alone.
    }

    template <>
    inline void resetClonedRuntimeState<UITabsComponent>(UITabsComponent& c)
    {
        c.previousTabIndex = -1;
    }

    template <>
    inline void resetClonedRuntimeState<UISliderComponent>(UISliderComponent& c)
    {
        c.currentState = UISliderState::Normal;
        c.currentHandleDisplayColor = c.handleNormalColor;
        c.isDragging = false;
        c.dragStartMousePos = {0.0f, 0.0f};
        c.dragStartValue = 0.0f;
    }

    template <>
    inline void resetClonedRuntimeState<UIProgressBarComponent>(UIProgressBarComponent& c)
    {
        c.displayValue = c.value;
        c.completedFired = (c.value >= c.maxValue);
    }

    template <>
    inline void resetClonedRuntimeState<UIDraggableComponent>(UIDraggableComponent& c)
    {
        c.isDragging = false;
        c.dragStartMousePos = {0.0f, 0.0f};
        c.dragStartEntityPos = {0.0f, 0.0f};
        c.currentGhostPos = {0.0f, 0.0f};
        c.ghostSize = {0.0f, 0.0f};
        // activeDragEntity is a class static, not part of the object value - leave it alone.
    }

    template <>
    inline void resetClonedRuntimeState<UIDropTargetComponent>(UIDropTargetComponent& c)
    {
        c.isHighlighted = false;
        c.isRejected = false;
    }

    template <>
    inline void resetClonedRuntimeState<UIWindowComponent>(UIWindowComponent& c)
    {
        c.isDraggingWindow = false;
        c.dragStartMousePos = {0.0f, 0.0f};
        c.dragStartAnchoredPos = {0.0f, 0.0f};
        c.closeHovered = false;
    }

    template <>
    inline void resetClonedRuntimeState<UIListViewComponent>(UIListViewComponent& c)
    {
        // Instances belong to the source list; the clone rebuilds its own.
        c.itemInstances.clear();
        c.pool.clear();
        c.selectedIndex = -1;
        c.needsReconcile = true;
    }

    template <>
    inline void resetClonedRuntimeState<UIAnimationComponent>(UIAnimationComponent& c)
    {
        c.isPlaying = false;
        c.isPaused = false;
        c.elapsedTime = 0.0f;
        c.startedFired = false;
        c.completedFired = false;
    }

    namespace detail
    {
        template <typename T>
        inline void cloneOne(const scene::Entity& src, scene::Entity& dst)
        {
            // Scripts are reattached by the caller (via the scripting system), not value-copied.
            if constexpr (std::is_same_v<T, ScriptComponent>)
            {
                return;
            }
            else
            {
                if (src.hasComponent<T>())
                {
                    T copy = src.getComponent<T>();
                    resetClonedRuntimeState<T>(copy);
                    dst.addOrReplaceComponent<T>(std::move(copy));
                }
            }
        }

        template <typename... Ts>
        inline void cloneAll(const scene::Entity& src, scene::Entity& dst, entt::type_list<Ts...>)
        {
            (cloneOne<Ts>(src, dst), ...);
        }
    }

    // Copies every present optional component from src to dst (excluding ScriptComponent),
    // resetting runtime-only fields. UUID/Name/Parent/Children/Transform are not in
    // OptionalComponents and are therefore left untouched - the caller owns identity and
    // hierarchy. New components are picked up automatically once added to OptionalComponents.
    inline void cloneOptionalComponents(const scene::Entity& src, scene::Entity& dst)
    {
        detail::cloneAll(src, dst, OptionalComponents{});
    }
}
