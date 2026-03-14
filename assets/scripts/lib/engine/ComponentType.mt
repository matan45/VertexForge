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

    // Light components
    public static final string DIRECTIONAL_LIGHT = "DirectionalLight";
    public static final string POINT_LIGHT = "PointLight";
    public static final string SPOT_LIGHT = "SpotLight";

    // Physics components
    public static final string COLLIDER = "Collider";
    public static final string RIGID_BODY = "RigidBody";

    // Animation and effects
    public static final string ANIMATOR = "Animator";
    public static final string VFX = "VFX";
    public static final string PHYSICS_ANIMATION = "PhysicsAnimation";
    public static final string IK_TARGET = "IKTarget";

    // Navigation
    public static final string NAVMESH_AGENT = "NavmeshAgent";

    // Controller
    public static final string CONTROLLER = "Controller";

    // Socket attachment
    public static final string SOCKET_ATTACHMENT = "SocketAttachment";
    public static final string SOCKET_OVERRIDE = "SocketOverride";

    // Editor/rendering components
    public static final string IBL = "IBL";
    public static final string BILLBOARD = "Billboard";
    public static final string TEXT = "Text";
    public static final string DECAL = "Decal";

    // UI components
    public static final string UI_CANVAS = "UICanvas";
    public static final string UI_RECT = "UIRect";
    public static final string UI_IMAGE = "UIImage";
    public static final string UI_SCROLL = "UIScroll";
    public static final string UI_LAYOUT_GROUP = "UILayoutGroup";
    public static final string UI_LABEL = "UILabel";
    public static final string UI_BUTTON = "UIButton";
    public static final string UI_TEXT_INPUT = "UITextInput";
    public static final string UI_CHECKBOX = "UICheckbox";
    public static final string UI_DROPDOWN = "UIDropdown";
    public static final string UI_TABS = "UITabs";
    public static final string UI_SLIDER = "UISlider";
    public static final string UI_PROGRESS_BAR = "UIProgressBar";
}
