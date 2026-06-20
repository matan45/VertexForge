// LoadingScreen - drives a loading-screen UICanvas from real engine progress
// (VK-1268). Attach this script to a manager entity that lives in a PERSISTENT
// canvas/scene, and author the UI entities it drives (by name) below.
//
// Required UI entities in your scene/prefab (rename via the fields below):
//   "LoadingScreen" - root entity of the loading UICanvas (toggled on/off)
//   "LoadingBar"    - entity with a UIProgressBar component
//   "LoadingStatus" - entity with a UILabel for the phase status text
//   "LoadingTip"    - entity with a UILabel for rotating tips / lore
//
// NOTE: during a full scene LOAD the current scene (and any UI in it) is torn
// down, so the loading UI must live in a persistent/additive canvas to survive
// the transition. A fully decoupled overlay that paints with no live scene is a
// separate engine task (VK-1268 Phase 2). This controller works today for
// streaming transitions and persistent-canvas setups.

import * from "engine/Entity.mt";
import * from "engine/UI.mt";
import * from "engine/Loading.mt";

@Script
public class LoadingScreen {
    // Entity names to resolve (author these in your scene).
    private string rootName;
    private string barName;
    private string statusName;
    private string tipName;

    private int rootId;
    private int barId;
    private int statusId;
    private int tipId;

    private bool visible;
    private float shownTime;       // seconds the screen has been visible
    private float minDisplayTime;  // hold at least this long to avoid a flash
    private float tipTimer;
    private float tipInterval;
    private int tipIndex;
    private int tipCount;

    public constructor() {
        this.rootName = "LoadingScreen";
        this.barName = "LoadingBar";
        this.statusName = "LoadingStatus";
        this.tipName = "LoadingTip";
        this.rootId = -1;
        this.barId = -1;
        this.statusId = -1;
        this.tipId = -1;
        this.visible = false;
        this.shownTime = 0.0;
        this.minDisplayTime = 1.5;
        this.tipTimer = 0.0;
        this.tipInterval = 4.0;
        this.tipIndex = 0;
        this.tipCount = 3;
    }

    public function onStart(): void {
        this.rootId = Entity::findByName(this.rootName);
        this.barId = Entity::findByName(this.barName);
        this.statusId = Entity::findByName(this.statusName);
        this.tipId = Entity::findByName(this.tipName);
        this.hide();
    }

    public function onUpdate(float deltaTime): void {
        bool active = Loading::isActive();

        if (active && !this.visible) {
            this.show();
        }

        if (this.visible) {
            this.shownTime = this.shownTime + deltaTime;

            // Drive the bar + status text from real aggregated progress.
            if (this.barId >= 0) {
                UI::setProgressBarValue(this.barId, Loading::getProgress());
            }
            if (this.statusId >= 0) {
                UI::setLabelText(this.statusId, Loading::getPhaseLabel());
            }

            // Rotate tips so a long load doesn't feel frozen.
            this.tipTimer = this.tipTimer + deltaTime;
            if (this.tipTimer >= this.tipInterval) {
                this.tipTimer = 0.0;
                this.tipIndex = (this.tipIndex + 1) % this.tipCount;
                if (this.tipId >= 0) {
                    UI::setLabelText(this.tipId, this.tipText(this.tipIndex));
                }
            }

            // Hide once loading is done AND the minimum display time elapsed.
            if (!active && this.shownTime >= this.minDisplayTime) {
                this.hide();
            }
        }
    }

    private function show(): void {
        this.visible = true;
        this.shownTime = 0.0;
        this.tipTimer = 0.0;
        if (this.rootId >= 0) {
            Entity::setActive(this.rootId, true);
        }
        if (this.tipId >= 0) {
            UI::setLabelText(this.tipId, this.tipText(this.tipIndex));
        }
    }

    private function hide(): void {
        this.visible = false;
        if (this.rootId >= 0) {
            Entity::setActive(this.rootId, false);
        }
    }

    // Tips / lore. Replace with your game's text (kept as a function instead of
    // an array literal to stay portable across mType versions).
    private function tipText(int i): string {
        if (i == 0) {
            return "Tip: Scout a system before you commit your colony ship.";
        }
        if (i == 1) {
            return "Lore: The last fleets drift between dying stars.";
        }
        return "Tip: Queue commands by holding the order key.";
    }

    public function onDestroy(): void {
    }
}
