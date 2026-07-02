// UIElement - Typed wrapper over the entity's UI widget(s).
// Stateless view forwarding to the UI static facade. One wrapper covers the
// common cross-widget surface plus the high-frequency widget accessors; drop
// down to the UI statics (via entityId) for the full per-widget API.

import * from "../UI.mt";
import * from "../Entity.mt";
import * from "Component.mt";

public class UIElement extends Component {
    public constructor(int entityId) : super(entityId) {
    }

    @Override
    public function exists(): bool {
        // Any UI widget qualifies; UIRect is the common denominator all
        // widgets carry for layout.
        return Entity::hasComponent(this.entityId, "UIRect") ||
               Entity::hasComponent(this.entityId, "UICanvas");
    }

    // --- label ---

    public function labelText(): string {
        return UI::getLabelText(this.entityId);
    }

    public function setLabelText(string text): void {
        UI::setLabelText(this.entityId, text);
    }

    // --- button ---

    public function isButtonHovered(): bool {
        return UI::isButtonHovered(this.entityId);
    }

    public function isButtonPressed(): bool {
        return UI::isButtonPressed(this.entityId);
    }

    public function setButtonInteractable(bool interactable): void {
        UI::setButtonInteractable(this.entityId, interactable);
    }

    // --- text input ---

    public function textInputText(): string {
        return UI::getTextInputText(this.entityId);
    }

    public function setTextInputText(string text): void {
        UI::setTextInputText(this.entityId, text);
    }

    public function setTextInputInteractable(bool interactable): void {
        UI::setTextInputInteractable(this.entityId, interactable);
    }

    // --- checkbox ---

    public function isChecked(): bool {
        return UI::isCheckboxChecked(this.entityId);
    }

    public function setChecked(bool checked): void {
        UI::setCheckboxChecked(this.entityId, checked);
    }

    // --- slider / progress bar ---

    public function sliderValue(): float {
        return UI::getSliderValue(this.entityId);
    }

    public function setSliderValue(float value): void {
        UI::setSliderValue(this.entityId, value);
    }

    public function progressValue(): float {
        return UI::getProgressBarValue(this.entityId);
    }

    public function setProgressValue(float value): void {
        UI::setProgressBarValue(this.entityId, value);
    }

    // --- image ---

    public function setImageTexture(string texturePath): void {
        UI::setImageTexture(this.entityId, texturePath);
    }

    // --- animation ---

    public function playAnimation(): void {
        UI::playAnimation(this.entityId);
    }

    public function stopAnimation(): void {
        UI::stopAnimation(this.entityId);
    }

    public function isAnimationPlaying(): bool {
        return UI::isAnimationPlaying(this.entityId);
    }
}
