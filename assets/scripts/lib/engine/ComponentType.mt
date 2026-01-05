// ComponentType - Static constants for component type names
// Use these instead of hardcoded strings for type safety
//
// Usage examples:
//   Entity::hasComponent(self, ComponentType::CAMERA);
//   Entity::addComponent(self, ComponentType::MESH);
//   Entity::removeComponent(self, ComponentType::AUDIO_3D);

public class ComponentType {
    // Core components
    public static final string TRANSFORM = "Transform";
    public static final string CAMERA = "Camera";
    public static final string MESH = "Mesh";
    public static final string MATERIAL = "Material";

    // Scripting
    public static final string SCRIPT = "Script";

    // Audio components
    public static final string AUDIO_2D = "AudioSource2D";
    public static final string AUDIO_3D = "AudioSource3D";

    // Editor/rendering components
    public static final string IBL = "IBL";
    public static final string BILLBOARD = "Billboard";
}
