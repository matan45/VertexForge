// UI - Static utility class for UI component operations
// Works with entity IDs (int) to query and control UI button state
//
// Usage examples:
//   int buttonId = 42;
//   bool hovered = UI::isButtonHovered(buttonId);
//   bool pressed = UI::isButtonPressed(buttonId);
//   int state = UI::getButtonState(buttonId);  // 0=Normal, 1=Hovered, 2=Pressed, 3=Disabled
//   UI::setButtonInteractable(buttonId, false); // Disable button
//
// Button State Constants:
//   BUTTON_NORMAL   = 0
//   BUTTON_HOVERED  = 1
//   BUTTON_PRESSED  = 2
//   BUTTON_DISABLED = 3

public class UI {
    // ============================================
    // Button State Constants
    // ============================================
    public static const int BUTTON_NORMAL = 0;
    public static const int BUTTON_HOVERED = 1;
    public static const int BUTTON_PRESSED = 2;
    public static const int BUTTON_DISABLED = 3;

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
    public static function getLabelText(int entityId): String {
        return _native_ui_getLabelText(entityId);
    }

    // Set the text of a UILabel on the given entity
    public static function setLabelText(int entityId, String text): void {
        _native_ui_setLabelText(entityId, text);
    }

    // ============================================
    // Checkbox State Constants
    // ============================================
    public static const int CHECKBOX_NORMAL = 0;
    public static const int CHECKBOX_HOVERED = 1;
    public static const int CHECKBOX_DISABLED = 2;

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
    public static function getCheckboxLabelText(int entityId): String {
        return _native_ui_getCheckboxLabelText(entityId);
    }
}
