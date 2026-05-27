#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <entt/entt.hpp>
#include "../asset/AssetRef.hpp"

namespace components
{
    enum class UIScaleMode : uint8_t
    {
        ConstantPixelSize,
        ScaleWithScreenSize
    };

    struct UICanvasComponent
    {
        float referenceWidth = 1920.0f;
        float referenceHeight = 1080.0f;
        UIScaleMode scaleMode = UIScaleMode::ScaleWithScreenSize;
        float pixelsPerUnit = 100.0f;
        int sortOrder = 0;
    };

    struct UIRectComponent
    {
        glm::vec2 anchorMin{0.0f, 0.0f};
        glm::vec2 anchorMax{1.0f, 1.0f};
        glm::vec2 pivot{0.5f, 0.5f};
        glm::vec2 sizeDelta{0.0f, 0.0f};
        glm::vec2 anchoredPosition{0.0f, 0.0f};
    };

    enum class UIImageType : uint8_t
    {
        Simple,
        Sliced,
        Tiled
    };

    struct UIImageComponent
    {
        asset::AssetRef textureRef;
        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
        entt::entity renderTextureSource = entt::null;
        std::string renderTextureSourceName;

        UIImageType imageType = UIImageType::Simple;
        glm::vec4 border{0.0f, 0.0f, 0.0f, 0.0f}; // left, right, top, bottom in source texture pixels
        uint32_t sourceWidth = 0;
        uint32_t sourceHeight = 0;
    };

    enum class ScrollbarVisibility : uint8_t
    {
        Auto,          // Show only when content overflows
        AlwaysVisible, // Always show
        Hidden         // Never show (still scrollable)
    };

    struct UIScrollComponent
    {
        // Config (serialized)
        bool horizontalScrollEnabled = false;
        bool verticalScrollEnabled = true;
        ScrollbarVisibility horizontalScrollbarVisibility = ScrollbarVisibility::Auto;
        ScrollbarVisibility verticalScrollbarVisibility = ScrollbarVisibility::Auto;
        float scrollSensitivity = 1.0f;

        // Runtime state (NOT serialized)
        glm::vec2 scrollOffset{0.0f, 0.0f};
        glm::vec2 contentSize{0.0f, 0.0f};
        glm::vec2 viewportSize{0.0f, 0.0f};
        glm::vec4 computedScissorRect{0.0f, 0.0f, 0.0f, 0.0f}; // x, y, width, height

        // Drag state
        bool isDragging = false;
        uint8_t dragAxis = 0; // 0=horizontal, 1=vertical
        glm::vec2 dragStartScrollOffset{0.0f, 0.0f};
        glm::vec2 dragStartMousePos{0.0f, 0.0f};
    };

    enum class LayoutDirection : uint8_t
    {
        Vertical,
        Horizontal,
        Grid
    };

    enum class ChildAlignment : uint8_t
    {
        Start,
        Center,
        End
    };

    struct UILayoutGroupComponent
    {
        LayoutDirection direction = LayoutDirection::Vertical;
        float spacing = 0.0f;
        glm::vec4 padding{0.0f, 0.0f, 0.0f, 0.0f}; // left, right, top, bottom
        ChildAlignment childAlignment = ChildAlignment::Start;
        int constraintCount = 2; // columns (Grid mode only)
    };

    enum class HorizontalAlignment : uint8_t
    {
        Left,
        Center,
        Right
    };

    enum class VerticalAlignment : uint8_t
    {
        Top,
        Middle,
        Bottom
    };

    enum class TextOverflow : uint8_t
    {
        Overflow,
        Clip,
        Ellipsis
    };

    enum class FontStyle : uint8_t
    {
        Normal,
        Bold,
        Italic,
        BoldItalic
    };

    struct UILabelComponent
    {
        std::string text = "Label";
        asset::AssetRef fontRef;
        float fontSize = 16.0f;
        FontStyle fontStyle = FontStyle::Normal;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        HorizontalAlignment horizontalAlignment = HorizontalAlignment::Left;
        VerticalAlignment verticalAlignment = VerticalAlignment::Top;
        TextOverflow overflow = TextOverflow::Overflow;
        bool wordWrap = true;
        float lineSpacing = 1.0f;
        float letterSpacing = 0.0f;
    };

    enum class UIButtonState : uint8_t
    {
        Normal,
        Hovered,
        Pressed,
        Disabled
    };

    struct UIButtonComponent
    {
        // Per-state colors
        glm::vec4 normalColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 hoveredColor{0.9f, 0.9f, 0.9f, 1.0f};
        glm::vec4 pressedColor{0.7f, 0.7f, 0.7f, 1.0f};
        glm::vec4 disabledColor{0.5f, 0.5f, 0.5f, 0.5f};

        // Per-state texture refs (invalid = use color only)
        asset::AssetRef normalTextureRef;
        asset::AssetRef hoverTextureRef;
        asset::AssetRef pressedTextureRef;
        asset::AssetRef disabledTextureRef;

        // Config
        float colorTransitionDuration = 0.1f;
        bool interactable = true;

        // Runtime state (NOT serialized)
        UIButtonState currentState = UIButtonState::Normal;
        glm::vec4 currentDisplayColor{1.0f, 1.0f, 1.0f, 1.0f}; // lerped display color
    };

    enum class UITextInputState : uint8_t
    {
        Normal,
        Hovered,
        Focused,
        Disabled
    };

    struct UITextInputComponent
    {
        // Config (serialized)
        std::string text;
        std::string placeholderText = "Enter text...";
        asset::AssetRef fontRef;
        float fontSize = 16.0f;
        glm::vec4 textColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 placeholderColor{0.5f, 0.5f, 0.5f, 0.7f};

        // Per-state background colors
        glm::vec4 normalColor{0.2f, 0.2f, 0.2f, 1.0f};
        glm::vec4 hoveredColor{0.25f, 0.25f, 0.25f, 1.0f};
        glm::vec4 focusedColor{0.15f, 0.15f, 0.3f, 1.0f};
        glm::vec4 disabledColor{0.15f, 0.15f, 0.15f, 0.5f};

        float colorTransitionDuration = 0.1f;
        bool interactable = true;
        int maxLength = 0; // 0 = unlimited

        // Caret and selection
        glm::vec4 selectionColor{0.3f, 0.5f, 0.8f, 0.5f};
        glm::vec4 caretColor{1.0f, 1.0f, 1.0f, 1.0f};
        float caretWidth = 2.0f;
        float caretBlinkRate = 0.53f; // seconds per blink cycle

        // Runtime state (NOT serialized)
        UITextInputState currentState = UITextInputState::Normal;
        int cursorPosition = 0;
        int selectionStart = -1; // -1 = no selection
        int selectionEnd = -1;   // -1 = no selection
        float caretBlinkTimer = 0.0f;
        bool caretVisible = true;
        glm::vec4 currentDisplayColor{0.2f, 0.2f, 0.2f, 1.0f};
        float scrollOffsetX = 0.0f; // horizontal scroll for text wider than field
    };

    enum class UICheckboxState : uint8_t
    {
        Normal,
        Hovered,
        Disabled
    };

    struct UICheckboxComponent
    {
        // Checked state
        bool isChecked = false;

        // Radio group (empty = independent checkbox, non-empty = radio mode)
        std::string groupName;
        bool allowUncheck = true; // In radio group: false = can't uncheck by clicking

        // Per-state colors
        glm::vec4 uncheckedColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 checkedColor{0.3f, 0.7f, 1.0f, 1.0f};
        glm::vec4 hoveredColor{0.9f, 0.9f, 0.9f, 1.0f};
        glm::vec4 disabledColor{0.5f, 0.5f, 0.5f, 0.5f};

        // Per-state texture refs (invalid = use color only)
        asset::AssetRef uncheckedTextureRef;
        asset::AssetRef checkedTextureRef;
        asset::AssetRef hoveredTextureRef;
        asset::AssetRef disabledTextureRef;

        // Config
        float colorTransitionDuration = 0.1f;
        bool interactable = true;
        bool labelToggle = false; // If true, clicking child UILabel also toggles

        // Runtime state (NOT serialized)
        UICheckboxState currentState = UICheckboxState::Normal;
        glm::vec4 currentDisplayColor{1.0f, 1.0f, 1.0f, 1.0f};
    };

    enum class UIDropdownState : uint8_t
    {
        Normal,
        Hovered,
        Open,
        Disabled
    };

    struct DropdownOption
    {
        std::string text;
        asset::AssetRef iconRef; // optional .vfImage asset, invalid = no icon
    };

    struct UIDropdownComponent
    {
        // Config (serialized)
        std::vector<DropdownOption> options;
        int selectedIndex = -1; // -1 = nothing selected
        std::string placeholderText = "Select...";
        int maxVisibleItems = 5;
        bool interactable = true;

        // Header state colors
        glm::vec4 normalColor{0.25f, 0.25f, 0.25f, 1.0f};
        glm::vec4 hoveredColor{0.3f, 0.3f, 0.3f, 1.0f};
        glm::vec4 openColor{0.2f, 0.2f, 0.35f, 1.0f};
        glm::vec4 disabledColor{0.15f, 0.15f, 0.15f, 0.5f};

        // List colors
        glm::vec4 listBackgroundColor{0.18f, 0.18f, 0.18f, 1.0f};
        glm::vec4 itemNormalColor{0.18f, 0.18f, 0.18f, 0.0f};
        glm::vec4 itemHoveredColor{0.3f, 0.5f, 0.8f, 0.5f};

        // Font settings
        asset::AssetRef fontRef;
        float fontSize = 16.0f;

        float colorTransitionDuration = 0.1f;

        // Runtime state (NOT serialized)
        UIDropdownState currentState = UIDropdownState::Normal;
        bool isOpen = false;
        int hoveredOptionIndex = -1;
        glm::vec4 currentDisplayColor{0.25f, 0.25f, 0.25f, 1.0f};
        float listScrollOffset = 0.0f;

        // Global singleton tracker: only one dropdown can be open at a time
        static inline entt::entity activeDropdownEntity = entt::null;
    };

    enum class TabBarPosition : uint8_t
    {
        Top,
        Bottom,
        Left,
        Right
    };

    struct UITabsComponent
    {
        // Config (serialized)
        TabBarPosition tabBarPosition = TabBarPosition::Top;
        int activeTabIndex = 0;

        // Runtime state (NOT serialized)
        int previousTabIndex = -1;
    };

    enum class UISliderOrientation : uint8_t
    {
        Horizontal,
        Vertical
    };

    enum class UISliderState : uint8_t
    {
        Normal,
        Hovered,
        Pressed,
        Disabled
    };

    struct UISliderComponent
    {
        // Value config (serialized)
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float value = 0.5f;
        float stepSize = 0.0f; // 0 = continuous (no snapping)
        UISliderOrientation orientation = UISliderOrientation::Horizontal;
        bool clickTrackToSet = true;

        // Handle appearance (serialized)
        float handleSizeRatio = 0.08f; // handle width as fraction of track length
        glm::vec4 handleNormalColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 handleHoveredColor{0.9f, 0.9f, 0.9f, 1.0f};
        glm::vec4 handlePressedColor{0.7f, 0.7f, 0.7f, 1.0f};
        glm::vec4 handleDisabledColor{0.5f, 0.5f, 0.5f, 0.5f};
        asset::AssetRef handleNormalTextureRef;
        asset::AssetRef handleHoveredTextureRef;
        asset::AssetRef handlePressedTextureRef;
        asset::AssetRef handleDisabledTextureRef;

        // Fill appearance (serialized)
        glm::vec4 fillColor{0.3f, 0.5f, 0.8f, 1.0f};
        asset::AssetRef fillTextureRef;

        // Config (serialized)
        float colorTransitionDuration = 0.1f;
        bool interactable = true;

        // Runtime state (NOT serialized)
        UISliderState currentState = UISliderState::Normal;
        glm::vec4 currentHandleDisplayColor{1.0f, 1.0f, 1.0f, 1.0f};
        bool isDragging = false;
        glm::vec2 dragStartMousePos{0.0f, 0.0f};
        float dragStartValue = 0.0f;
    };

    struct UIProgressBarComponent
    {
        // Value config (serialized)
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float value = 0.0f;
        UISliderOrientation orientation = UISliderOrientation::Horizontal;
        bool invertDirection = false;

        // Smooth interpolation (serialized)
        bool smoothInterpolation = false;
        float interpolationSpeed = 5.0f;

        // Track appearance (serialized)
        glm::vec4 trackColor{0.2f, 0.2f, 0.2f, 1.0f};
        asset::AssetRef trackTextureRef;

        // Fill appearance (serialized)
        glm::vec4 fillColor{0.3f, 0.5f, 0.8f, 1.0f};
        asset::AssetRef fillTextureRef;

        // Runtime state (NOT serialized)
        float displayValue = 0.0f;
        bool completedFired = false;
    };

    // ========== UI Animation / Tweening ==========

    enum class UIEasingFunction : uint8_t
    {
        Linear,
        EaseIn,
        EaseOut,
        EaseInOut,
        Bounce,
        Elastic
    };

    enum class UIAnimationLoopMode : uint8_t
    {
        Once,
        Loop,
        PingPong
    };

    enum class UITweenProperty : uint8_t
    {
        Opacity,
        PositionX,
        PositionY,
        ScaleX,
        ScaleY,
        Rotation,
        ColorR,
        ColorG,
        ColorB,
        ColorA
    };

    enum class UIAnimationNodeType : uint8_t
    {
        Clip,
        Parallel,
        Sequence
    };

    struct UIAnimationClip
    {
        UITweenProperty property = UITweenProperty::Opacity;
        float startValue = 0.0f;
        float endValue = 1.0f;
        float duration = 1.0f;
        float delay = 0.0f;
        UIEasingFunction easing = UIEasingFunction::Linear;
        UIAnimationLoopMode loopMode = UIAnimationLoopMode::Once;
    };

    struct UIAnimationNode
    {
        UIAnimationNodeType type = UIAnimationNodeType::Clip;
        UIAnimationClip clip;
        std::vector<UIAnimationNode> children;
        UIAnimationLoopMode loopMode = UIAnimationLoopMode::Once;
    };

    struct UIMaskComponent
    {
        asset::AssetRef maskTextureRef;  // alpha texture used as mask shape
        float alphaThreshold = 0.5f;
        bool showMaskGraphic = false; // render the mask shape visually
    };

    struct UIDraggableComponent
    {
        // Config (serialized)
        float ghostOpacity = 0.5f;
        glm::vec2 ghostOffset{0.0f, 0.0f};
        bool constrainToParent = true;
        std::string dragTag;

        // Runtime state (NOT serialized)
        bool isDragging = false;
        glm::vec2 dragStartMousePos{0.0f, 0.0f};
        glm::vec2 dragStartEntityPos{0.0f, 0.0f};
        glm::vec2 currentGhostPos{0.0f, 0.0f};
        glm::vec2 ghostSize{0.0f, 0.0f};

        // Global singleton: only one entity can be dragged at a time
        static inline entt::entity activeDragEntity = entt::null;
    };

    struct UIDropTargetComponent
    {
        // Config (serialized)
        std::string acceptTag;                              // empty = accept all
        glm::vec4 highlightColor{0.3f, 0.7f, 1.0f, 0.3f}; // overlay when valid drag hovers
        glm::vec4 rejectColor{1.0f, 0.2f, 0.2f, 0.3f};    // overlay when incompatible drag hovers
        bool interactable = true;

        // Runtime state (NOT serialized)
        bool isHighlighted = false;
        bool isRejected = false;  // true when hovered by incompatible drag
    };

    struct UIAnimationComponent
    {
        // Config (serialized)
        UIAnimationNode rootNode;
        bool autoPlay = false;

        // Runtime state (NOT serialized)
        bool isPlaying = false;
        bool isPaused = false;
        float elapsedTime = 0.0f;
        bool startedFired = false;
        bool completedFired = false;
    };
}
