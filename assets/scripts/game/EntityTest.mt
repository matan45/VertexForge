// EntityTest - Test script demonstrating Entity API usage
// Attach this script to an entity to test entity/component queries

import * from "../lib/engine/Log.mt";
import * from "../lib/engine/Time.mt";
import * from "../lib/engine/Entity.mt";

@Script
public class EntityTest {
    private int selfId;
    private float rotationSpeed;
    private float moveSpeed;

    public constructor() {
        this.rotationSpeed = 45.0;  // degrees per second
        this.moveSpeed = 2.0;       // units per second
    }

    public function onStart(): void {
        // Get reference to this entity
        this.selfId = Entity::self();
        Log::info("EntityTest started on: " + Entity::getName(this.selfId));

        // Log current position
        float[] pos = Entity::getPosition(this.selfId);
        Log::info("Initial position: (" + parsePrimitive(pos[0]) + ", " + parsePrimitive(pos[1]) + ", " + parsePrimitive(pos[2]) + ")");

        // Log components on this entity
        string[] components = Entity::getComponents(this.selfId);
        Log::info("Components on this entity:");
        for (int i = 0; i < components.length; i = i + 1) {
            Log::info("  - " + components[i]);
        }

        // Try to find other entities
        int cameraId = Entity::findByName("Camera");
        if (cameraId >= 0 && Entity::isValid(cameraId)) {
            Log::info("Found camera: " + Entity::getName(cameraId));
        }

        // Find all entities with Mesh component
        int[] meshEntityIds = Entity::findWithComponent("Mesh");
        Log::info("Found " + parsePrimitive(meshEntityIds.length) + " entities with Mesh component");

        // Check hierarchy
        int parentId = Entity::getParent(this.selfId);
        if (parentId >= 0 && Entity::isValid(parentId)) {
            Log::info("Parent entity: " + Entity::getName(parentId));
        }

        int[] childIds = Entity::getChildren(this.selfId);
        if (childIds.length > 0) {
            Log::info("Children count: " + parsePrimitive(childIds.length));
        }
    }

    public function onUpdate(float deltaTime): void {
        // Rotate the entity over time
        float[] rot = Entity::getRotation(this.selfId);
        float newY = rot[1] + this.rotationSpeed * deltaTime;
        Entity::setRotation(this.selfId, rot[0], newY, rot[2]);
    }

    public function onDestroy(): void {
        Log::info("EntityTest destroyed on: " + Entity::getName(this.selfId));
    }

    // Test function to create a new entity
    public function spawnChild(): int {
        int childId = Entity::create("SpawnedChild");
        Log::info("Created new entity: " + Entity::getName(childId));
        return childId;
    }
}
