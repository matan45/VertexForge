// PerfHud - example runtime perf overlay driver (VK-1534)
//
// The engine ships this as a copy-in convenience; it is NOT auto-instantiated (per the
// "engine proves the API, logic lives in mType" rule). Runtime UI is authored as scene
// entities, so you supply an existing UILabel and PerfHud drives its text/visibility
// from the Stats sinks. Off by default (gated on Stats::getHudEnabled()).
//
// Setup:
//   1. Add a UILabel entity to your scene (e.g. top-left, monospace font), name it.
//   2. From a persistent @Script, call PerfHud::update(labelId) every frame and toggle
//      the overlay with a key.
//
// Example @Script:
//   int hudLabel = -1;
//   function onStart(): void { hudLabel = Entity::findByName("PerfHudLabel"); }
//   function onUpdate(float dt): void {
//       if (Input::isKeyReleased(Key::F3)) { Stats::setHudEnabled(!Stats::getHudEnabled()); }
//       PerfHud::update(hudLabel);
//   }

public class PerfHud {
    public constructor() {
    }

    // Show/hide the label per the persisted gfx.hud toggle and, when visible, refresh
    // it with the current FPS / CPU ms / GPU ms / draw-call line. Call once per frame.
    public static function update(int labelEntity): void {
        if (labelEntity < 0) {
            return;
        }
        bool on = Stats::getHudEnabled();
        Entity::setActive(labelEntity, on);
        if (on) {
            UI::setLabelText(labelEntity, Stats::getHudLine());
        }
    }
}
