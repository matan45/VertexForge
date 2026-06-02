// UI - Static utility class for UI component operations
// Works with entity IDs (int) to query and control UI component state
//
// Usage examples:
//   int buttonId = 42;
//   bool hovered = UI::isButtonHovered(buttonId);
//   bool pressed = UI::isButtonPressed(buttonId);
//   int state = UI::getButtonState(buttonId);  // 0=Normal, 1=Hovered, 2=Pressed, 3=Disabled
//   UI::setButtonInteractable(buttonId, false); // Disable button
//
//   int dropdownId = 50;
//   int selected = UI::getDropdownSelectedIndex(dropdownId);
//   String value = UI::getDropdownSelectedValue(dropdownId);
//   UI::setDropdownSelectedIndex(dropdownId, 2);
//   UI::setDropdownOptions(dropdownId, "Option A|Option B|Option C");
//   UI::openDropdown(dropdownId);
//
// Button State Constants:
//   BUTTON_NORMAL   = 0
//   BUTTON_HOVERED  = 1
//   BUTTON_PRESSED  = 2
//   BUTTON_DISABLED = 3
//
// Dropdown State Constants:
//   DROPDOWN_NORMAL   = 0
//   DROPDOWN_HOVERED  = 1
//   DROPDOWN_OPEN     = 2
//   DROPDOWN_DISABLED = 3

public class UI {
    // ============================================
    // Button State Constants
    // ============================================
    public static final int BUTTON_NORMAL = 0;
    public static final int BUTTON_HOVERED = 1;
    public static final int BUTTON_PRESSED = 2;
    public static final int BUTTON_DISABLED = 3;

    public constructor() {
    }

    // ============================================
    // Button Queries
    // ============================================

    // Check if a button entity is currently hovered
    public static function isButtonHovered(int entityId): bool {
        return _native_ui_isButtonHovered(entityId);
    }

    // Check if a button entity is currently pressed
    public static function isButtonPressed(int entityId): bool {
        return _native_ui_isButtonPressed(entityId);
    }

    // Get button state as int (BUTTON_NORMAL, BUTTON_HOVERED, BUTTON_PRESSED, BUTTON_DISABLED)
    public static function getButtonState(int entityId): int {
        return _native_ui_getButtonState(entityId);
    }

    // ============================================
    // Button Control
    // ============================================

    // Set whether a button is interactable (enabled/disabled)
    public static function setButtonInteractable(int entityId, bool interactable): void {
        _native_ui_setButtonInteractable(entityId, interactable);
    }

    // ============================================
    // Label Text
    // ============================================

    // Get the text of a UILabel on the given entity
    public static function getLabelText(int entityId): string {
        return _native_ui_getLabelText(entityId);
    }

    // Set the text of a UILabel on the given entity
    public static function setLabelText(int entityId, string text): void {
        _native_ui_setLabelText(entityId, text);
    }

    // ============================================
    // Label Style Constants (VK-1352)
    // ============================================
    // Font style (UILabelData.fontStyle)
    public static final int LABEL_STYLE_NORMAL = 0;
    public static final int LABEL_STYLE_BOLD = 1;
    public static final int LABEL_STYLE_ITALIC = 2;
    public static final int LABEL_STYLE_BOLD_ITALIC = 3;
    // Horizontal alignment (UILabelData.horizontalAlignment)
    public static final int LABEL_ALIGN_LEFT = 0;
    public static final int LABEL_ALIGN_CENTER = 1;
    public static final int LABEL_ALIGN_RIGHT = 2;
    // Vertical alignment (UILabelData.verticalAlignment)
    public static final int LABEL_VALIGN_TOP = 0;
    public static final int LABEL_VALIGN_MIDDLE = 1;
    public static final int LABEL_VALIGN_BOTTOM = 2;
    // Overflow (UILabelData.overflow)
    public static final int LABEL_OVERFLOW = 0;
    public static final int LABEL_CLIP = 1;
    public static final int LABEL_ELLIPSIS = 2;

    // ============================================
    // Label Properties (VK-1352)
    // ============================================
    // Each restyles a UILabel at runtime; the change is reflected next frame.

    // Set the font size (in points) of a UILabel
    public static function setLabelFontSize(int entityId, float size): void {
        _native_ui_setLabelFontSize(entityId, size);
    }

    // Get the font size of a UILabel (0 if no label)
    public static function getLabelFontSize(int entityId): float {
        return _native_ui_getLabelFontSize(entityId);
    }

    // Set the text color (RGBA, each channel 0..1)
    public static function setLabelColor(int entityId, float r, float g, float b, float a): void {
        _native_ui_setLabelColor(entityId, r, g, b, a);
    }

    // Get the text color as a float[4] = [r, g, b, a]
    public static function getLabelColor(int entityId): float[] {
        return _native_ui_getLabelColor(entityId);
    }

    // Set the font style (LABEL_STYLE_NORMAL / BOLD / ITALIC / BOLD_ITALIC)
    public static function setLabelStyle(int entityId, int style): void {
        _native_ui_setLabelStyle(entityId, style);
    }

    // Get the font style as int (see LABEL_STYLE_* constants)
    public static function getLabelStyle(int entityId): int {
        return _native_ui_getLabelStyle(entityId);
    }

    // Set horizontal + vertical alignment (LABEL_ALIGN_* / LABEL_VALIGN_*)
    public static function setLabelAlignment(int entityId, int horizontal, int vertical): void {
        _native_ui_setLabelAlignment(entityId, horizontal, vertical);
    }

    // Get alignment as a float[2] = [horizontal, vertical] (see LABEL_ALIGN_* / LABEL_VALIGN_*)
    public static function getLabelAlignment(int entityId): float[] {
        return _native_ui_getLabelAlignment(entityId);
    }

    // Set the overflow mode (LABEL_OVERFLOW / LABEL_CLIP / LABEL_ELLIPSIS)
    public static function setLabelOverflow(int entityId, int overflow): void {
        _native_ui_setLabelOverflow(entityId, overflow);
    }

    // Get the overflow mode as int (see LABEL_OVERFLOW / LABEL_CLIP / LABEL_ELLIPSIS)
    public static function getLabelOverflow(int entityId): int {
        return _native_ui_getLabelOverflow(entityId);
    }

    // Enable/disable word wrapping
    public static function setLabelWordWrap(int entityId, bool wrap): void {
        _native_ui_setLabelWordWrap(entityId, wrap);
    }

    // Get whether word wrapping is enabled
    public static function getLabelWordWrap(int entityId): bool {
        return _native_ui_getLabelWordWrap(entityId);
    }

    // Set line + letter spacing multipliers
    public static function setLabelSpacing(int entityId, float line, float letter): void {
        _native_ui_setLabelSpacing(entityId, line, letter);
    }

    // Get spacing as a float[2] = [lineSpacing, letterSpacing]
    public static function getLabelSpacing(int entityId): float[] {
        return _native_ui_getLabelSpacing(entityId);
    }

    // Set the font by asset path (e.g. an imported font). Must be a registered asset.
    public static function setLabelFont(int entityId, string fontPath): void {
        _native_ui_setLabelFont(entityId, fontPath);
    }

    // Get the current font asset path (empty string if none)
    public static function getLabelFont(int entityId): string {
        return _native_ui_getLabelFont(entityId);
    }

    // ============================================
    // Image Texture
    // ============================================

    // Point a UIImage on the given entity at a texture asset by path at runtime.
    // The path must be a registered/imported asset (e.g. an .vfImage). The UI
    // renderer resolves the texture per-frame, so the swap takes effect live.
    public static function setImageTexture(int entityId, string assetPath): void {
        _native_ui_setImageTexture(entityId, assetPath);
    }

    // Get the current texture asset path of a UIImage (empty string if none).
    public static function getImageTexture(int entityId): string {
        return _native_ui_getImageTexture(entityId);
    }

    // ============================================
    // Checkbox State Constants
    // ============================================
    public static final int CHECKBOX_NORMAL = 0;
    public static final int CHECKBOX_HOVERED = 1;
    public static final int CHECKBOX_DISABLED = 2;

    // ============================================
    // Checkbox Queries
    // ============================================

    // Check if a checkbox entity is currently checked
    public static function isCheckboxChecked(int entityId): bool {
        return _native_ui_isCheckboxChecked(entityId);
    }

    // Set the checked state of a checkbox entity
    public static function setCheckboxChecked(int entityId, bool checked): void {
        _native_ui_setCheckboxChecked(entityId, checked);
    }

    // Get checkbox state as int (CHECKBOX_NORMAL, CHECKBOX_HOVERED, CHECKBOX_DISABLED)
    public static function getCheckboxState(int entityId): int {
        return _native_ui_getCheckboxState(entityId);
    }

    // ============================================
    // Checkbox Control
    // ============================================

    // Set whether a checkbox is interactable (enabled/disabled)
    public static function setCheckboxInteractable(int entityId, bool interactable): void {
        _native_ui_setCheckboxInteractable(entityId, interactable);
    }

    // Get the label text of a checkbox's child UILabel (useful in onCheckboxToggled callbacks)
    public static function getCheckboxLabelText(int entityId): string {
        return _native_ui_getCheckboxLabelText(entityId);
    }

    // ============================================
    // Dropdown State Constants
    // ============================================
    public static final int DROPDOWN_NORMAL = 0;
    public static final int DROPDOWN_HOVERED = 1;
    public static final int DROPDOWN_OPEN = 2;
    public static final int DROPDOWN_DISABLED = 3;

    // ============================================
    // Dropdown Queries
    // ============================================

    // Get the currently selected index (-1 if none selected)
    public static function getDropdownSelectedIndex(int entityId): int {
        return _native_ui_getDropdownSelectedIndex(entityId);
    }

    // Get the text of the currently selected option (empty string if none)
    public static function getDropdownSelectedValue(int entityId): string {
        return _native_ui_getDropdownSelectedValue(entityId);
    }

    // Get dropdown state as int (DROPDOWN_NORMAL, DROPDOWN_HOVERED, DROPDOWN_OPEN, DROPDOWN_DISABLED)
    public static function getDropdownState(int entityId): int {
        return _native_ui_getDropdownState(entityId);
    }

    // ============================================
    // Dropdown Control
    // ============================================

    // Set the selected index of a dropdown
    public static function setDropdownSelectedIndex(int entityId, int index): void {
        _native_ui_setDropdownSelectedIndex(entityId, index);
    }

    // Set whether a dropdown is interactable (enabled/disabled)
    public static function setDropdownInteractable(int entityId, bool interactable): void {
        _native_ui_setDropdownInteractable(entityId, interactable);
    }

    // Set dropdown options using pipe-delimited string (e.g. "Option A|Option B|Option C")
    // Note: option text cannot contain the '|' character
    public static function setDropdownOptions(int entityId, string options): void {
        _native_ui_setDropdownOptions(entityId, options);
    }

    // Open the dropdown (closes any other open dropdown)
    public static function openDropdown(int entityId): void {
        _native_ui_openDropdown(entityId);
    }

    // Close the dropdown
    public static function closeDropdown(int entityId): void {
        _native_ui_closeDropdown(entityId);
    }

    // ============================================
    // Tabs Bar Position Constants
    // ============================================
    public static final int TAB_BAR_TOP = 0;
    public static final int TAB_BAR_BOTTOM = 1;
    public static final int TAB_BAR_LEFT = 2;
    public static final int TAB_BAR_RIGHT = 3;

    // ============================================
    // Tabs Queries
    // ============================================

    // Get the currently active tab index
    public static function getTabsActiveIndex(int entityId): int {
        return _native_ui_getTabsActiveIndex(entityId);
    }

    // Get the tab bar position (TAB_BAR_TOP, TAB_BAR_BOTTOM, TAB_BAR_LEFT, TAB_BAR_RIGHT)
    public static function getTabsBarPosition(int entityId): int {
        return _native_ui_getTabsBarPosition(entityId);
    }

    // ============================================
    // Tabs Control
    // ============================================

    // Set the active tab index (triggers tab switching and notifications)
    public static function setTabsActiveIndex(int entityId, int index): void {
        _native_ui_setTabsActiveIndex(entityId, index);
    }

    // ============================================
    // Slider Orientation Constants
    // ============================================
    public static final int SLIDER_HORIZONTAL = 0;
    public static final int SLIDER_VERTICAL = 1;

    // ============================================
    // Slider State Constants
    // ============================================
    public static final int SLIDER_NORMAL = 0;
    public static final int SLIDER_HOVERED = 1;
    public static final int SLIDER_PRESSED = 2;
    public static final int SLIDER_DISABLED = 3;

    // ============================================
    // Slider Queries
    // ============================================

    // Get the current value of a slider
    public static function getSliderValue(int entityId): float {
        return _native_ui_getSliderValue(entityId);
    }

    // Get slider state as int (SLIDER_NORMAL, SLIDER_HOVERED, SLIDER_PRESSED, SLIDER_DISABLED)
    public static function getSliderState(int entityId): int {
        return _native_ui_getSliderState(entityId);
    }

    // Get the minimum value of a slider
    public static function getSliderMin(int entityId): float {
        return _native_ui_getSliderMin(entityId);
    }

    // Get the maximum value of a slider
    public static function getSliderMax(int entityId): float {
        return _native_ui_getSliderMax(entityId);
    }

    // ============================================
    // Slider Control
    // ============================================

    // Set the value of a slider (clamped to min/max)
    public static function setSliderValue(int entityId, float value): void {
        _native_ui_setSliderValue(entityId, value);
    }

    // Set whether a slider is interactable (enabled/disabled)
    public static function setSliderInteractable(int entityId, bool interactable): void {
        _native_ui_setSliderInteractable(entityId, interactable);
    }

    // Set the min and max range of a slider
    public static function setSliderMinMax(int entityId, float min, float max): void {
        _native_ui_setSliderMinMax(entityId, min, max);
    }

    // ============================================
    // Progress Bar Queries
    // ============================================

    // Get the current target value of a progress bar
    public static function getProgressBarValue(int entityId): float {
        return _native_ui_getProgressBarValue(entityId);
    }

    // Get the current display value (may differ from target when smooth interpolation is active)
    public static function getProgressBarDisplayValue(int entityId): float {
        return _native_ui_getProgressBarDisplayValue(entityId);
    }

    // Get the minimum value of a progress bar
    public static function getProgressBarMin(int entityId): float {
        return _native_ui_getProgressBarMin(entityId);
    }

    // Get the maximum value of a progress bar
    public static function getProgressBarMax(int entityId): float {
        return _native_ui_getProgressBarMax(entityId);
    }

    // Check if the progress bar has reached its maximum value
    public static function isProgressBarCompleted(int entityId): bool {
        return _native_ui_isProgressBarCompleted(entityId);
    }

    // ============================================
    // Progress Bar Control
    // ============================================

    // Set the value of a progress bar (clamped to min/max)
    public static function setProgressBarValue(int entityId, float value): void {
        _native_ui_setProgressBarValue(entityId, value);
    }

    // Set the min and max range of a progress bar
    public static function setProgressBarMinMax(int entityId, float min, float max): void {
        _native_ui_setProgressBarMinMax(entityId, min, max);
    }

    // ============================================
    // Drag & Drop
    // ============================================

    // Cancel the current drag operation (e.g. when drop target is occupied)
    // Returns true if a drag was active and cancelled, false if nothing was being dragged
    public static function cancelDrag(): bool {
        return _native_ui_cancelDrag();
    }
}
