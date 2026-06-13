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
    // Pointer Queries
    // ============================================

    // True when the cursor is over any visible UI element this frame.
    // Use to skip world raycasts/picking while the pointer is on the HUD.
    public static function isPointerOverUI(): bool {
        return _native_ui_isPointerOverUI();
    }

    // ============================================
    // Rect Control
    // ============================================

    // Position/size a UIRect in viewport pixels (top-left origin, y down) — the same
    // pixel space as Input::getViewportMouseX/Y. Works regardless of the rect's
    // authored anchors/pivot. Used for runtime-driven overlays (e.g. drag boxes).
    public static function setRectPixels(int entityId, float x, float y, float w, float h): bool {
        return _native_ui_setRectPixels(entityId, x, y, w, h);
    }

    // Resolved on-screen rect of a UIRect in viewport pixels (top-left origin,
    // y down) — the same pixel space as setRectPixels and Input::getViewportMouseX/Y.
    // Returns [valid, x, y, w, h]; valid <= 0.5 means no UIRect or no viewport.
    public static function getRectPixels(int entityId): float[] {
        return _native_ui_getRectPixels(entityId);
    }

    // Authored UIRect fields in CANVAS UNITS (anchors normalized [0,1], plus pivot,
    // sizeDelta and anchoredPosition in canvas units). Unlike getRectPixels this is
    // viewport/extent-independent: two elements expressed in the same basis stay
    // aligned under any viewport (e.g. the editor play panel vs the framebuffer).
    // Returns [valid, anchorMinX, anchorMinY, anchorMaxX, anchorMaxY, pivotX, pivotY,
    // sizeDeltaX, sizeDeltaY, anchoredX, anchoredY]; valid <= 0.5 means no UIRect.
    public static function getRectData(int entityId): float[] {
        return _native_ui_getRectData(entityId);
    }

    // Set the authored UIRect fields in canvas units (anchors normalized [0,1]).
    // Preserves the element's existing blocksRaycast flag. Returns true on success.
    public static function setRectData(int entityId,
        float anchorMinX, float anchorMinY, float anchorMaxX, float anchorMaxY,
        float pivotX, float pivotY, float sizeDeltaX, float sizeDeltaY,
        float anchoredX, float anchoredY): bool {
        return _native_ui_setRectData(entityId,
            anchorMinX, anchorMinY, anchorMaxX, anchorMaxY,
            pivotX, pivotY, sizeDeltaX, sizeDeltaY, anchoredX, anchoredY);
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

    // Enable/disable rich text markup parsing ([b], [i], [color=#RRGGBB])
    public static function setLabelRichText(int entityId, bool richText): void {
        _native_ui_setLabelRichText(entityId, richText);
    }

    // Get whether rich text markup parsing is enabled
    public static function getLabelRichText(int entityId): bool {
        return _native_ui_getLabelRichText(entityId);
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
    // Image / Button Tint Colors
    // ============================================

    // Set a UIImage's color tint (RGBA 0..1). The tint multiplies the texture,
    // so use white (1,1,1,1) to show a texture at full brightness.
    public static function setImageColor(int entityId, float r, float g, float b, float a): void {
        _native_ui_setImageColor(entityId, r, g, b, a);
    }

    // Get a UIImage's color tint as a float[4] = [r, g, b, a]
    public static function getImageColor(int entityId): float[] {
        return _native_ui_getImageColor(entityId);
    }

    // Set a UIButton's normal/hovered/pressed colors in one call (each RGBA 0..1).
    // These multiply the button's image, so use light colors over a texture skin.
    public static function setButtonColors(int entityId,
        float nr, float ng, float nb, float na,
        float hr, float hg, float hb, float ha,
        float pr, float pg, float pb, float pa): void {
        _native_ui_setButtonColors(entityId, nr, ng, nb, na, hr, hg, hb, ha, pr, pg, pb, pa);
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

    // ============================================
    // Theme / Style
    // ============================================

    // Assign a .vfTheme asset to a canvas and apply it to the whole subtree.
    // Pass an empty path to clear the theme.
    public static function setCanvasTheme(int canvasId, string themePath): bool {
        return _native_ui_setCanvasTheme(canvasId, themePath);
    }

    // Get the canvas theme as an asset path (empty string if none)
    public static function getCanvasTheme(int canvasId): string {
        return _native_ui_getCanvasTheme(canvasId);
    }

    // Set the theme style key on an element (adds a UIStyle component if
    // missing) and apply it immediately when the canvas has a theme
    public static function setStyleKey(int entityId, string styleKey): bool {
        return _native_ui_setStyleKey(entityId, styleKey);
    }

    // Get the element's style key (empty string if it has no UIStyle component)
    public static function getStyleKey(int entityId): string {
        return _native_ui_getStyleKey(entityId);
    }

    // Re-apply canvas themes. Pass a canvas entity id, or -1 for every themed
    // canvas in the scene. Returns the number of styled elements touched.
    public static function reapplyTheme(int canvasId): int {
        return _native_ui_reapplyTheme(canvasId);
    }

    // ============================================
    // Tooltip
    // ============================================

    // Set the tooltip text (Text mode)
    public static function setTooltipText(int entityId, string text): void {
        _native_ui_setTooltipText(entityId, text);
    }

    // Get the tooltip text (empty string if no tooltip component)
    public static function getTooltipText(int entityId): string {
        return _native_ui_getTooltipText(entityId);
    }

    // Enable/disable the tooltip without removing it
    public static function setTooltipEnabled(int entityId, bool enabled): void {
        _native_ui_setTooltipEnabled(entityId, enabled);
    }

    // Set the hover delay before the tooltip appears (seconds)
    public static function setTooltipDelay(int entityId, float seconds): void {
        _native_ui_setTooltipDelay(entityId, seconds);
    }

    // Set the tooltip font (Text mode). A valid font is required for the bubble
    // text to render.
    public static function setTooltipFont(int entityId, string fontPath): void {
        _native_ui_setTooltipFont(entityId, fontPath);
    }

    // Set the tooltip font size (points, Text mode)
    public static function setTooltipFontSize(int entityId, float size): void {
        _native_ui_setTooltipFontSize(entityId, size);
    }

    // Set the tooltip padding in pixels (left, right, top, bottom). The Text-mode
    // bubble hugs its text, so larger left/right padding makes the bubble wider.
    public static function setTooltipPadding(int entityId, float left, float right, float top, float bottom): void {
        _native_ui_setTooltipPadding(entityId, left, right, top, bottom);
    }

    // ============================================
    // Window
    // ============================================

    // Open a window: activates the entity; modal windows also block the UI
    // beneath them. Fires IUIWindowListener.onWindowOpened.
    public static function openWindow(int entityId): bool {
        return _native_ui_openWindow(entityId);
    }

    // Close a window: deactivates the entity and fires onWindowClosed
    public static function closeWindow(int entityId): bool {
        return _native_ui_closeWindow(entityId);
    }

    // True when the window entity is effectively active
    public static function isWindowOpen(int entityId): bool {
        return _native_ui_isWindowOpen(entityId);
    }

    // Toggle modal behavior (dim backdrop + interaction blocking)
    public static function setWindowModal(int entityId, bool modal): void {
        _native_ui_setWindowModal(entityId, modal);
    }

    // Set the title bar text
    public static function setWindowTitle(int entityId, string title): void {
        _native_ui_setWindowTitle(entityId, title);
    }

    // Get the title bar text (empty string if no window component)
    public static function getWindowTitle(int entityId): string {
        return _native_ui_getWindowTitle(entityId);
    }

    // ============================================
    // List View
    // ============================================

    // Set the bound item count; the engine instantiates/pools item-template
    // copies and the layout group positions them
    public static function setListItemCount(int entityId, int count): bool {
        return _native_ui_setListItemCount(entityId, count);
    }

    // Get the bound item count
    public static function getListItemCount(int entityId): int {
        return _native_ui_getListItemCount(entityId);
    }

    // Get the root entity of the item instance at index (-1 on miss).
    // Walk its children (Entity::getChildren + Entity::getName) and use
    // UI::setLabelText/setImageTexture to fill in per-item data.
    public static function getListItem(int entityId, int index): int {
        return _native_ui_getListItem(entityId, index);
    }

    // Assign the .vfPrefab item template by asset path (rebuilds all items)
    public static function setListItemTemplate(int entityId, string prefabPath): bool {
        return _native_ui_setListItemTemplate(entityId, prefabPath);
    }

    // Get the selected item index (-1 = none)
    public static function getListSelectedIndex(int entityId): int {
        return _native_ui_getListSelectedIndex(entityId);
    }

    // Select an item by index (-1 clears). Fires IUIListViewListener.
    public static function setListSelectedIndex(int entityId, int index): bool {
        return _native_ui_setListSelectedIndex(entityId, index);
    }
}
