// EntityTest - Test script demonstrating Entity API usage
// Attach this script to an entity to test entity/component queries

import * from "../lib/engine/Log.mt";
import * from "../lib/engine/Entity.mt";
import * from "../lib/engine/ComponentType.mt";

@Script
public class EntityTest {
    private int selfId;
    private float rotationSpeed;
    private float moveSpeed;
    private int spawnedEntityId;

    public constructor() {
        this.rotationSpeed = 45.0;  // degrees per second
        this.moveSpeed = 2.0;       // units per second
        this.spawnedEntityId = -1;
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

        // Check for specific components using ComponentType constants
        if (Entity::hasComponent(this.selfId, ComponentType::MESH)) {
            Log::info("This entity has a Mesh component");
        }
        if (Entity::hasComponent(this.selfId, ComponentType::CAMERA)) {
            Log::info("This entity has a Camera component");
        }

        // Find all entities with Camera component
        int[] cameraEntities = Entity::findWithComponent(ComponentType::CAMERA);
        Log::info("Found " + parsePrimitive(cameraEntities.length) + " entities with Camera component");

        // Find all entities with Mesh component
        int[] meshEntities = Entity::findWithComponent(ComponentType::MESH);
        Log::info("Found " + parsePrimitive(meshEntities.length) + " entities with Mesh component");

        // Check hierarchy
        int parentId = Entity::getParent(this.selfId);
        if (parentId >= 0 && Entity::isValid(parentId)) {
            Log::info("Parent entity: " + Entity::getName(parentId));
        }

        int[] childIds = Entity::getChildren(this.selfId);
        if (childIds.length > 0) {
            Log::info("Children count: " + parsePrimitive(childIds.length));
        }

        // Demonstrate adding/removing components
        this.testComponentOperations();
    }

    private function testComponentOperations(): void {
        // Create a new entity and add components to it
        this.spawnedEntityId = Entity::create("TestAudioEntity");
        Log::info("Created entity: " + Entity::getName(this.spawnedEntityId));

        // Add an AudioSource3D component
        bool added = Entity::addComponent(this.spawnedEntityId, ComponentType::AUDIO_3D);
        if (added) {
            Log::info("Added AudioSource3D component");
        }

        // Position it nearby
        float[] myPos = Entity::getPosition(this.selfId);
        Entity::setPosition(this.spawnedEntityId, myPos[0] + 2.0, myPos[1], myPos[2]);

        // Verify the component was added
        if (Entity::hasComponent(this.spawnedEntityId, ComponentType::AUDIO_3D)) {
            Log::info("Verified: Entity has AudioSource3D component");
        }

        // List all components on the new entity
        string[] newEntityComponents = Entity::getComponents(this.spawnedEntityId);
        Log::info("Components on spawned entity:");
        for (int i = 0; i < newEntityComponents.length; i = i + 1) {
            Log::info("  - " + newEntityComponents[i]);
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

        // Clean up spawned entity
        if (this.spawnedEntityId >= 0 && Entity::isValid(this.spawnedEntityId)) {
            // Remove the audio component first
            Entity::removeComponent(this.spawnedEntityId, ComponentType::AUDIO_3D);
            Log::info("Removed AudioSource3D from spawned entity");

            // Then destroy the entity
            Entity::destroy(this.spawnedEntityId);
            Log::info("Destroyed spawned entity");
        }
    }
}
