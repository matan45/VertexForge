// HudController - Example of a HUD driven by a script (VK-1659).
//
// Expects three widgets on a canvas, found by NAME when the script starts
// (Entity::findByName returns the first entity with that name, -1 if none):
// a ProgressBar "HealthBar", a Label "ScoreLabel" and a Button "PauseButton".
// Health drains and refills; the score grows by 10 every second. The Pause
// button freezes gameplay time: onUpdate stops, but button callbacks still
// run, so the same button resumes. Button events reach EVERY script that
// implements IUIButtonListener, so compare the button's entity name.

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/Entity.mt";
import * from "../../lib/engine/UI.mt";
import * from "../../lib/engine/Time.mt";
import * from "../../lib/engine/IUIButtonListener.mt";

@Script
public class HudController extends Behaviour implements IUIButtonListener {
    private int healthBar = -1;
    private int scoreLabel = -1;
    private float health = 100.0;
    private int score = 0;
    private float scoreTimer = 0.0;

    public constructor() : super() {
    }

    @Override
    public function onStart(): void {
        this.healthBar = Entity::findByName("HealthBar");
        this.scoreLabel = Entity::findByName("ScoreLabel");
        if (this.healthBar < 0 || this.scoreLabel < 0) {
            this.logWarn("name the widgets HealthBar and ScoreLabel");
            return;
        }
        UI::setProgressBarMinMax(this.healthBar, 0.0, 100.0);
    }

    @Override
    public function onUpdate(float deltaTime): void {
        this.health = this.health - 5.0 * deltaTime;
        if (this.health < 0.0) {
            this.health = 100.0;
        }
        this.scoreTimer = this.scoreTimer + deltaTime;
        if (this.scoreTimer >= 1.0) {
            this.scoreTimer = this.scoreTimer - 1.0;
            this.score = this.score + 10;
        }
        if (this.healthBar >= 0) {
            UI::setProgressBarValue(this.healthBar, this.health);
        }
        if (this.scoreLabel >= 0) {
            UI::setLabelText(this.scoreLabel, "Score: " + parsePrimitive(this.score));
        }
    }

    @Override
    public function onButtonClicked(int buttonEntityId, string entityName): void {
        if (entityName == "PauseButton") {
            if (Time::isFrozen()) {
                Time::unfreeze();
                UI::setLabelText(buttonEntityId, "Pause");
            } else {
                Time::freeze();
                UI::setLabelText(buttonEntityId, "Resume");
            }
            this.log("pause toggled");
        }
    }

    @Override
    public function onButtonPressed(int buttonEntityId, string entityName): void {
    }

    @Override
    public function onButtonReleased(int buttonEntityId, string entityName): void {
    }

    @Override
    public function onButtonHoverEnter(int buttonEntityId, string entityName): void {
    }

    @Override
    public function onButtonHoverExit(int buttonEntityId, string entityName): void {
    }
}
