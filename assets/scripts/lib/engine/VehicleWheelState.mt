import * from "../math/Matrix4f.mt";

public class VehicleWheelState {
    public bool exists;
    public float rotationAngle;
    public float steerAngle;
    public float suspensionLength;
    public float angularVelocity;
    public bool hasContact;
    public Matrix4f worldTransform;

    public constructor() {
        this.exists = false;
        this.rotationAngle = 0.0;
        this.steerAngle = 0.0;
        this.suspensionLength = 0.0;
        this.angularVelocity = 0.0;
        this.hasContact = false;
        this.worldTransform = Matrix4f::identity();
    }

    public constructor(bool exists, float rotationAngle, float steerAngle,
                       float suspensionLength, float angularVelocity,
                       bool hasContact, Matrix4f worldTransform) {
        this.exists = exists;
        this.rotationAngle = rotationAngle;
        this.steerAngle = steerAngle;
        this.suspensionLength = suspensionLength;
        this.angularVelocity = angularVelocity;
        this.hasContact = hasContact;
        this.worldTransform = worldTransform;
    }
}
