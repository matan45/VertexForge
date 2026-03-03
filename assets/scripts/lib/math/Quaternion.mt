// Quaternion - Unit quaternion for 3D rotations
// Represents rotations without gimbal lock
// Format: q = w + xi + yj + zk (w is the scalar part, xyz is the vector part)

public value class Quaternion {
    public float x;
    public float y;
    public float z;
    public float w;

    // Constructors
    public constructor(float x, float y, float z, float w) {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    public constructor() {
        // Identity quaternion (no rotation)
        this.x = 0.0;
        this.y = 0.0;
        this.z = 0.0;
        this.w = 1.0;
    }

    // Quaternion operations
    public function add(Quaternion other): Quaternion {
        return new Quaternion(
            this.x + other.x,
            this.y + other.y,
            this.z + other.z,
            this.w + other.w
        );
    }

    public function subtract(Quaternion other): Quaternion {
        return new Quaternion(
            this.x - other.x,
            this.y - other.y,
            this.z - other.z,
            this.w - other.w
        );
    }

    public function multiply(float scalar): Quaternion {
        return new Quaternion(
            this.x * scalar,
            this.y * scalar,
            this.z * scalar,
            this.w * scalar
        );
    }

    // Hamilton product (quaternion multiplication)
    public function multiply(Quaternion other): Quaternion {
        return new Quaternion(
            this.w * other.x + this.x * other.w + this.y * other.z - this.z * other.y,
            this.w * other.y - this.x * other.z + this.y * other.w + this.z * other.x,
            this.w * other.z + this.x * other.y - this.y * other.x + this.z * other.w,
            this.w * other.w - this.x * other.x - this.y * other.y - this.z * other.z
        );
    }

    public function conjugate(): Quaternion {
        return new Quaternion(-this.x, -this.y, -this.z, this.w);
    }

    public function lengthSquared(): float {
        return this.x * this.x + this.y * this.y + this.z * this.z + this.w * this.w;
    }

    public function length(): float {
        return sqrt(this.lengthSquared());
    }

    public function normalize(): Quaternion {
        float len = this.length();
        if (len > 0.0) {
            float invLen = 1.0 / len;
            return new Quaternion(
                this.x * invLen,
                this.y * invLen,
                this.z * invLen,
                this.w * invLen
            );
        }
        return Quaternion.identity();
    }

    public function inverse(): Quaternion {
        float lenSq = this.lengthSquared();
        if (lenSq > 0.0) {
            float invLenSq = 1.0 / lenSq;
            return new Quaternion(
                -this.x * invLenSq,
                -this.y * invLenSq,
                -this.z * invLenSq,
                this.w * invLenSq
            );
        }
        return Quaternion.identity();
    }

    public function dot(Quaternion other): float {
        return this.x * other.x + this.y * other.y + this.z * other.z + this.w * other.w;
    }

    // Rotate a Vec3f by this quaternion
    public function rotate(Vec3f v): Vec3f {
        // q * v * q^(-1)
        // Optimized formula avoiding full quaternion multiplication
        float qx = this.x;
        float qy = this.y;
        float qz = this.z;
        float qw = this.w;

        // t = 2 * cross(q.xyz, v)
        float tx = 2.0 * (qy * v.z - qz * v.y);
        float ty = 2.0 * (qz * v.x - qx * v.z);
        float tz = 2.0 * (qx * v.y - qy * v.x);

        // result = v + w * t + cross(q.xyz, t)
        return new Vec3f(
            v.x + qw * tx + (qy * tz - qz * ty),
            v.y + qw * ty + (qz * tx - qx * tz),
            v.z + qw * tz + (qx * ty - qy * tx)
        );
    }

    // Linear interpolation (not normalized)
    public function lerp(Quaternion other, float t): Quaternion {
        return new Quaternion(
            this.x + (other.x - this.x) * t,
            this.y + (other.y - this.y) * t,
            this.z + (other.z - this.z) * t,
            this.w + (other.w - this.w) * t
        );
    }

    // Normalized linear interpolation
    public function nlerp(Quaternion other, float t): Quaternion {
        return this.lerp(other, t).normalize();
    }

    // Spherical linear interpolation
    public function slerp(Quaternion other, float t): Quaternion {
        float d = this.dot(other);

        // If the dot product is negative, negate one quaternion
        // to take the shorter path
        Quaternion target = other;
        if (d < 0.0) {
            target = new Quaternion(-other.x, -other.y, -other.z, -other.w);
            d = -d;
        }

        // If quaternions are very close, use linear interpolation
        if (d > 0.9995) {
            return this.nlerp(target, t);
        }

        // Clamp d to valid range for acos
        if (d > 1.0) { d = 1.0; }
        if (d < -1.0) { d = -1.0; }

        float theta = acos(d);
        float sinTheta = sin(theta);

        if (sinTheta < 0.0001) {
            return this.nlerp(target, t);
        }

        float s0 = sin((1.0 - t) * theta) / sinTheta;
        float s1 = sin(t * theta) / sinTheta;

        return new Quaternion(
            this.x * s0 + target.x * s1,
            this.y * s0 + target.y * s1,
            this.z * s0 + target.z * s1,
            this.w * s0 + target.w * s1
        );
    }

    // Get rotation angle in radians
    public function getAngle(): float {
        float w = this.w;
        if (w > 1.0) { w = 1.0; }
        if (w < -1.0) { w = -1.0; }
        return 2.0 * acos(w);
    }

    // Get rotation axis (undefined for identity quaternion)
    public function getAxis(): Vec3f {
        float sinHalfAngle = sqrt(1.0 - this.w * this.w);
        if (sinHalfAngle < 0.0001) {
            return new Vec3f(1.0, 0.0, 0.0);
        }
        return new Vec3f(
            this.x / sinHalfAngle,
            this.y / sinHalfAngle,
            this.z / sinHalfAngle
        );
    }

    // Convert to rotation matrix
    public function toMatrix3f(): Matrix3f {
        float x2 = this.x + this.x;
        float y2 = this.y + this.y;
        float z2 = this.z + this.z;

        float xx = this.x * x2;
        float xy = this.x * y2;
        float xz = this.x * z2;
        float yy = this.y * y2;
        float yz = this.y * z2;
        float zz = this.z * z2;
        float wx = this.w * x2;
        float wy = this.w * y2;
        float wz = this.w * z2;

        return new Matrix3f(
            1.0 - (yy + zz), xy - wz,         xz + wy,
            xy + wz,         1.0 - (xx + zz), yz - wx,
            xz - wy,         yz + wx,         1.0 - (xx + yy)
        );
    }

    public function toMatrix4f(): Matrix4f {
        float x2 = this.x + this.x;
        float y2 = this.y + this.y;
        float z2 = this.z + this.z;

        float xx = this.x * x2;
        float xy = this.x * y2;
        float xz = this.x * z2;
        float yy = this.y * y2;
        float yz = this.y * z2;
        float zz = this.z * z2;
        float wx = this.w * x2;
        float wy = this.w * y2;
        float wz = this.w * z2;

        return new Matrix4f(
            1.0 - (yy + zz), xy - wz,         xz + wy,         0.0,
            xy + wz,         1.0 - (xx + zz), yz - wx,         0.0,
            xz - wy,         yz + wx,         1.0 - (xx + yy), 0.0,
            0.0,             0.0,             0.0,             1.0
        );
    }

    // Convert to Euler angles (in radians, ZYX order)
    public function toEulerAngles(): Vec3f {
        // Roll (x-axis rotation)
        float sinRoll = 2.0 * (this.w * this.x + this.y * this.z);
        float cosRoll = 1.0 - 2.0 * (this.x * this.x + this.y * this.y);
        float roll = atan2(sinRoll, cosRoll);

        // Pitch (y-axis rotation)
        float sinPitch = 2.0 * (this.w * this.y - this.z * this.x);
        float pitch = 0.0;
        if (sinPitch >= 1.0) {
            pitch = 1.5707963;  // PI/2
        } else if (sinPitch <= -1.0) {
            pitch = -1.5707963;
        } else {
            pitch = asin(sinPitch);
        }

        // Yaw (z-axis rotation)
        float sinYaw = 2.0 * (this.w * this.z + this.x * this.y);
        float cosYaw = 1.0 - 2.0 * (this.y * this.y + this.z * this.z);
        float yaw = atan2(sinYaw, cosYaw);

        return new Vec3f(roll, pitch, yaw);
    }

    // Comparison
    public function equals(Quaternion other): bool {
        return this.x == other.x && this.y == other.y &&
               this.z == other.z && this.w == other.w;
    }

    // Utility
    public function toString(): string {
        return "Quaternion(" + parsePrimitive(this.x) + ", " + parsePrimitive(this.y) +
               ", " + parsePrimitive(this.z) + ", " + parsePrimitive(this.w) + ")";
    }

    public function hashCode(): int {
        int result = hashCode(this.x);
        result = result * 31 + hashCode(this.y);
        result = result * 31 + hashCode(this.z);
        result = result * 31 + hashCode(this.w);
        return result;
    }

    // Static factory methods
    public static function identity(): Quaternion {
        return new Quaternion(0.0, 0.0, 0.0, 1.0);
    }

    // Create from axis-angle representation (axis must be normalized)
    public static function fromAxisAngle(Vec3f axis, float radians): Quaternion {
        float halfAngle = radians * 0.5;
        float s = sin(halfAngle);
        return new Quaternion(
            axis.x * s,
            axis.y * s,
            axis.z * s,
            cos(halfAngle)
        );
    }

    // Create from Euler angles (in radians, ZYX order)
    public static function fromEulerAngles(float roll, float pitch, float yaw): Quaternion {
        float cy = cos(yaw * 0.5);
        float sy = sin(yaw * 0.5);
        float cp = cos(pitch * 0.5);
        float sp = sin(pitch * 0.5);
        float cr = cos(roll * 0.5);
        float sr = sin(roll * 0.5);

        return new Quaternion(
            sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy,
            cr * cp * cy + sr * sp * sy
        );
    }

    public static function fromEulerAnglesVec(Vec3f angles): Quaternion {
        return Quaternion.fromEulerAngles(angles.x, angles.y, angles.z);
    }

    // Create rotation from one vector to another
    public static function fromToRotation(Vec3f from, Vec3f to): Quaternion {
        Vec3f fromNorm = from.normalize();
        Vec3f toNorm = to.normalize();

        float d = fromNorm.dot(toNorm);

        // Vectors are parallel (same direction)
        if (d > 0.9999) {
            return Quaternion.identity();
        }

        // Vectors are parallel (opposite direction)
        if (d < -0.9999) {
            // Find perpendicular vector
            Vec3f axis = new Vec3f(1.0, 0.0, 0.0).cross(fromNorm);
            if (axis.lengthSquared() < 0.0001) {
                axis = new Vec3f(0.0, 1.0, 0.0).cross(fromNorm);
            }
            axis = axis.normalize();
            return Quaternion.fromAxisAngle(axis, 3.14159265);
        }

        Vec3f axis = fromNorm.cross(toNorm);
        float s = sqrt((1.0 + d) * 2.0);
        float invS = 1.0 / s;

        return new Quaternion(
            axis.x * invS,
            axis.y * invS,
            axis.z * invS,
            s * 0.5
        );
    }

    // Create look rotation (forward and up vectors)
    public static function lookRotation(Vec3f forward, Vec3f up): Quaternion {
        Vec3f f = forward.normalize();
        Vec3f r = up.cross(f).normalize();
        Vec3f u = f.cross(r);

        float m00 = r.x; float m01 = r.y; float m02 = r.z;
        float m10 = u.x; float m11 = u.y; float m12 = u.z;
        float m20 = f.x; float m21 = f.y; float m22 = f.z;

        float trace = m00 + m11 + m22;

        if (trace > 0.0) {
            float s = sqrt(trace + 1.0) * 2.0;
            return new Quaternion(
                (m12 - m21) / s,
                (m20 - m02) / s,
                (m01 - m10) / s,
                0.25 * s
            );
        } else if (m00 > m11 && m00 > m22) {
            float s = sqrt(1.0 + m00 - m11 - m22) * 2.0;
            return new Quaternion(
                0.25 * s,
                (m01 + m10) / s,
                (m20 + m02) / s,
                (m12 - m21) / s
            );
        } else if (m11 > m22) {
            float s = sqrt(1.0 + m11 - m00 - m22) * 2.0;
            return new Quaternion(
                (m01 + m10) / s,
                0.25 * s,
                (m12 + m21) / s,
                (m20 - m02) / s
            );
        } else {
            float s = sqrt(1.0 + m22 - m00 - m11) * 2.0;
            return new Quaternion(
                (m20 + m02) / s,
                (m12 + m21) / s,
                0.25 * s,
                (m01 - m10) / s
            );
        }
    }
}
