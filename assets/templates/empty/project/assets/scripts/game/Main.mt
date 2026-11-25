// Main - the project's first script. Attach it to an entity to see it run.
//
// Extends Behaviour (lib/engine/oop/Behaviour.mt), which supplies empty onStart / onUpdate / onDestroy:
// override only what you need, each with @Override.

import * from "../lib/engine/oop/Behaviour.mt";

@Script
public class Main extends Behaviour {
    public constructor() : super() {
    }

    @Override
    public function onStart(): void {
        this.log("Hello from {{PROJECT_NAME}}!");
    }

    @Override
    public function onUpdate(float deltaTime): void {
        // Called every frame.
    }

    // Optional hooks the engine calls when present (Behaviour does not declare them, so no @Override):
    // onLateUpdate(float deltaTime), onFixedUpdate(float fixedDeltaTime), onEnable(), onDisable().
}
