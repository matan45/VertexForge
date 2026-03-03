// Matrix4f - 4x4 floating-point matrix
// Row-major order: m[row][col] = data[row * 4 + col]
// Used for 3D transformations, projections, and view matrices

public value class Matrix4f {
    // Matrix elements stored in row-major order
    // | m00 m01 m02 m03 |
    // | m10 m11 m12 m13 |
    // | m20 m21 m22 m23 |
    // | m30 m31 m32 m33 |
    public float m00; public float m01; public float m02; public float m03;
    public float m10; public float m11; public float m12; public float m13;
    public float m20; public float m21; public float m22; public float m23;
    public float m30; public float m31; public float m32; public float m33;

    // Constructors
    public constructor() {
        // Initialize to identity
        this.m00 = 1.0; this.m01 = 0.0; this.m02 = 0.0; this.m03 = 0.0;
        this.m10 = 0.0; this.m11 = 1.0; this.m12 = 0.0; this.m13 = 0.0;
        this.m20 = 0.0; this.m21 = 0.0; this.m22 = 1.0; this.m23 = 0.0;
        this.m30 = 0.0; this.m31 = 0.0; this.m32 = 0.0; this.m33 = 1.0;
    }

    public constructor(float m00, float m01, float m02, float m03,
                       float m10, float m11, float m12, float m13,
                       float m20, float m21, float m22, float m23,
                       float m30, float m31, float m32, float m33) {
        this.m00 = m00; this.m01 = m01; this.m02 = m02; this.m03 = m03;
        this.m10 = m10; this.m11 = m11; this.m12 = m12; this.m13 = m13;
        this.m20 = m20; this.m21 = m21; this.m22 = m22; this.m23 = m23;
        this.m30 = m30; this.m31 = m31; this.m32 = m32; this.m33 = m33;
    }

    // Matrix operations
    public function add(Matrix4f other): Matrix4f {
        return new Matrix4f(
            this.m00 + other.m00, this.m01 + other.m01, this.m02 + other.m02, this.m03 + other.m03,
            this.m10 + other.m10, this.m11 + other.m11, this.m12 + other.m12, this.m13 + other.m13,
            this.m20 + other.m20, this.m21 + other.m21, this.m22 + other.m22, this.m23 + other.m23,
            this.m30 + other.m30, this.m31 + other.m31, this.m32 + other.m32, this.m33 + other.m33
        );
    }

    public function subtract(Matrix4f other): Matrix4f {
        return new Matrix4f(
            this.m00 - other.m00, this.m01 - other.m01, this.m02 - other.m02, this.m03 - other.m03,
            this.m10 - other.m10, this.m11 - other.m11, this.m12 - other.m12, this.m13 - other.m13,
            this.m20 - other.m20, this.m21 - other.m21, this.m22 - other.m22, this.m23 - other.m23,
            this.m30 - other.m30, this.m31 - other.m31, this.m32 - other.m32, this.m33 - other.m33
        );
    }

    public function multiply(float scalar): Matrix4f {
        return new Matrix4f(
            this.m00 * scalar, this.m01 * scalar, this.m02 * scalar, this.m03 * scalar,
            this.m10 * scalar, this.m11 * scalar, this.m12 * scalar, this.m13 * scalar,
            this.m20 * scalar, this.m21 * scalar, this.m22 * scalar, this.m23 * scalar,
            this.m30 * scalar, this.m31 * scalar, this.m32 * scalar, this.m33 * scalar
        );
    }

    public function multiply(Matrix4f other): Matrix4f {
        return new Matrix4f(
            this.m00 * other.m00 + this.m01 * other.m10 + this.m02 * other.m20 + this.m03 * other.m30,
            this.m00 * other.m01 + this.m01 * other.m11 + this.m02 * other.m21 + this.m03 * other.m31,
            this.m00 * other.m02 + this.m01 * other.m12 + this.m02 * other.m22 + this.m03 * other.m32,
            this.m00 * other.m03 + this.m01 * other.m13 + this.m02 * other.m23 + this.m03 * other.m33,

            this.m10 * other.m00 + this.m11 * other.m10 + this.m12 * other.m20 + this.m13 * other.m30,
            this.m10 * other.m01 + this.m11 * other.m11 + this.m12 * other.m21 + this.m13 * other.m31,
            this.m10 * other.m02 + this.m11 * other.m12 + this.m12 * other.m22 + this.m13 * other.m32,
            this.m10 * other.m03 + this.m11 * other.m13 + this.m12 * other.m23 + this.m13 * other.m33,

            this.m20 * other.m00 + this.m21 * other.m10 + this.m22 * other.m20 + this.m23 * other.m30,
            this.m20 * other.m01 + this.m21 * other.m11 + this.m22 * other.m21 + this.m23 * other.m31,
            this.m20 * other.m02 + this.m21 * other.m12 + this.m22 * other.m22 + this.m23 * other.m32,
            this.m20 * other.m03 + this.m21 * other.m13 + this.m22 * other.m23 + this.m23 * other.m33,

            this.m30 * other.m00 + this.m31 * other.m10 + this.m32 * other.m20 + this.m33 * other.m30,
            this.m30 * other.m01 + this.m31 * other.m11 + this.m32 * other.m21 + this.m33 * other.m31,
            this.m30 * other.m02 + this.m31 * other.m12 + this.m32 * other.m22 + this.m33 * other.m32,
            this.m30 * other.m03 + this.m31 * other.m13 + this.m32 * other.m23 + this.m33 * other.m33
        );
    }

    // Transform a Vec4f
    public function transform(Vec4f v): Vec4f {
        return new Vec4f(
            this.m00 * v.x + this.m01 * v.y + this.m02 * v.z + this.m03 * v.w,
            this.m10 * v.x + this.m11 * v.y + this.m12 * v.z + this.m13 * v.w,
            this.m20 * v.x + this.m21 * v.y + this.m22 * v.z + this.m23 * v.w,
            this.m30 * v.x + this.m31 * v.y + this.m32 * v.z + this.m33 * v.w
        );
    }

    // Transform a Vec3f as a point (w=1)
    public function transformPoint(Vec3f v): Vec3f {
        float w = this.m30 * v.x + this.m31 * v.y + this.m32 * v.z + this.m33;
        if (w != 0.0) {
            return new Vec3f(
                (this.m00 * v.x + this.m01 * v.y + this.m02 * v.z + this.m03) / w,
                (this.m10 * v.x + this.m11 * v.y + this.m12 * v.z + this.m13) / w,
                (this.m20 * v.x + this.m21 * v.y + this.m22 * v.z + this.m23) / w
            );
        }
        return new Vec3f(
            this.m00 * v.x + this.m01 * v.y + this.m02 * v.z + this.m03,
            this.m10 * v.x + this.m11 * v.y + this.m12 * v.z + this.m13,
            this.m20 * v.x + this.m21 * v.y + this.m22 * v.z + this.m23
        );
    }

    // Transform a Vec3f as a direction (w=0)
    public function transformDirection(Vec3f v): Vec3f {
        return new Vec3f(
            this.m00 * v.x + this.m01 * v.y + this.m02 * v.z,
            this.m10 * v.x + this.m11 * v.y + this.m12 * v.z,
            this.m20 * v.x + this.m21 * v.y + this.m22 * v.z
        );
    }

    public function transpose(): Matrix4f {
        return new Matrix4f(
            this.m00, this.m10, this.m20, this.m30,
            this.m01, this.m11, this.m21, this.m31,
            this.m02, this.m12, this.m22, this.m32,
            this.m03, this.m13, this.m23, this.m33
        );
    }

    public function determinant(): float {
        float a = this.m00 * this.m11 - this.m01 * this.m10;
        float b = this.m00 * this.m12 - this.m02 * this.m10;
        float c = this.m00 * this.m13 - this.m03 * this.m10;
        float d = this.m01 * this.m12 - this.m02 * this.m11;
        float e = this.m01 * this.m13 - this.m03 * this.m11;
        float f = this.m02 * this.m13 - this.m03 * this.m12;
        float g = this.m20 * this.m31 - this.m21 * this.m30;
        float h = this.m20 * this.m32 - this.m22 * this.m30;
        float i = this.m20 * this.m33 - this.m23 * this.m30;
        float j = this.m21 * this.m32 - this.m22 * this.m31;
        float k = this.m21 * this.m33 - this.m23 * this.m31;
        float l = this.m22 * this.m33 - this.m23 * this.m32;
        return a * l - b * k + c * j + d * i - e * h + f * g;
    }

    public function inverse(): Matrix4f {
        float a = this.m00 * this.m11 - this.m01 * this.m10;
        float b = this.m00 * this.m12 - this.m02 * this.m10;
        float c = this.m00 * this.m13 - this.m03 * this.m10;
        float d = this.m01 * this.m12 - this.m02 * this.m11;
        float e = this.m01 * this.m13 - this.m03 * this.m11;
        float f = this.m02 * this.m13 - this.m03 * this.m12;
        float g = this.m20 * this.m31 - this.m21 * this.m30;
        float h = this.m20 * this.m32 - this.m22 * this.m30;
        float i = this.m20 * this.m33 - this.m23 * this.m30;
        float j = this.m21 * this.m32 - this.m22 * this.m31;
        float k = this.m21 * this.m33 - this.m23 * this.m31;
        float l = this.m22 * this.m33 - this.m23 * this.m32;

        float det = a * l - b * k + c * j + d * i - e * h + f * g;
        if (det == 0.0) {
            return Matrix4f.identity();
        }

        float invDet = 1.0 / det;

        return new Matrix4f(
            (this.m11 * l - this.m12 * k + this.m13 * j) * invDet,
            (-this.m01 * l + this.m02 * k - this.m03 * j) * invDet,
            (this.m31 * f - this.m32 * e + this.m33 * d) * invDet,
            (-this.m21 * f + this.m22 * e - this.m23 * d) * invDet,

            (-this.m10 * l + this.m12 * i - this.m13 * h) * invDet,
            (this.m00 * l - this.m02 * i + this.m03 * h) * invDet,
            (-this.m30 * f + this.m32 * c - this.m33 * b) * invDet,
            (this.m20 * f - this.m22 * c + this.m23 * b) * invDet,

            (this.m10 * k - this.m11 * i + this.m13 * g) * invDet,
            (-this.m00 * k + this.m01 * i - this.m03 * g) * invDet,
            (this.m30 * e - this.m31 * c + this.m33 * a) * invDet,
            (-this.m20 * e + this.m21 * c - this.m23 * a) * invDet,

            (-this.m10 * j + this.m11 * h - this.m12 * g) * invDet,
            (this.m00 * j - this.m01 * h + this.m02 * g) * invDet,
            (-this.m30 * d + this.m31 * b - this.m32 * a) * invDet,
            (this.m20 * d - this.m21 * b + this.m22 * a) * invDet
        );
    }

    // Extract the upper-left 3x3 rotation/scale matrix
    public function toMatrix3f(): Matrix3f {
        return new Matrix3f(
            this.m00, this.m01, this.m02,
            this.m10, this.m11, this.m12,
            this.m20, this.m21, this.m22
        );
    }

    // Comparison
    public function equals(Matrix4f other): bool {
        return this.m00 == other.m00 && this.m01 == other.m01 && this.m02 == other.m02 && this.m03 == other.m03 &&
               this.m10 == other.m10 && this.m11 == other.m11 && this.m12 == other.m12 && this.m13 == other.m13 &&
               this.m20 == other.m20 && this.m21 == other.m21 && this.m22 == other.m22 && this.m23 == other.m23 &&
               this.m30 == other.m30 && this.m31 == other.m31 && this.m32 == other.m32 && this.m33 == other.m33;
    }

    // Utility
    public function toString(): string {
        return "Matrix4f[\n" +
               "  " + parsePrimitive(this.m00) + ", " + parsePrimitive(this.m01) + ", " + parsePrimitive(this.m02) + ", " + parsePrimitive(this.m03) + "\n" +
               "  " + parsePrimitive(this.m10) + ", " + parsePrimitive(this.m11) + ", " + parsePrimitive(this.m12) + ", " + parsePrimitive(this.m13) + "\n" +
               "  " + parsePrimitive(this.m20) + ", " + parsePrimitive(this.m21) + ", " + parsePrimitive(this.m22) + ", " + parsePrimitive(this.m23) + "\n" +
               "  " + parsePrimitive(this.m30) + ", " + parsePrimitive(this.m31) + ", " + parsePrimitive(this.m32) + ", " + parsePrimitive(this.m33) + "\n" +
               "]";
    }

    // Static factory methods
    public static function identity(): Matrix4f {
        return new Matrix4f();
    }

    public static function zero(): Matrix4f {
        return new Matrix4f(
            0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0
        );
    }

    // 3D transformation matrices
    public static function translation(float tx, float ty, float tz): Matrix4f {
        return new Matrix4f(
            1.0, 0.0, 0.0, tx,
            0.0, 1.0, 0.0, ty,
            0.0, 0.0, 1.0, tz,
            0.0, 0.0, 0.0, 1.0
        );
    }

    public static function translationFromVec(Vec3f t): Matrix4f {
        return Matrix4f.translation(t.x, t.y, t.z);
    }

    public static function scale(float sx, float sy, float sz): Matrix4f {
        return new Matrix4f(
            sx,  0.0, 0.0, 0.0,
            0.0, sy,  0.0, 0.0,
            0.0, 0.0, sz,  0.0,
            0.0, 0.0, 0.0, 1.0
        );
    }

    public static function scaleUniform(float s): Matrix4f {
        return Matrix4f.scale(s, s, s);
    }

    public static function scaleFromVec(Vec3f s): Matrix4f {
        return Matrix4f.scale(s.x, s.y, s.z);
    }

    public static function rotationX(float radians): Matrix4f {
        float c = cos(radians);
        float s = sin(radians);
        return new Matrix4f(
            1.0, 0.0, 0.0, 0.0,
            0.0, c,   -s,  0.0,
            0.0, s,   c,   0.0,
            0.0, 0.0, 0.0, 1.0
        );
    }

    public static function rotationY(float radians): Matrix4f {
        float c = cos(radians);
        float s = sin(radians);
        return new Matrix4f(
            c,   0.0, s,   0.0,
            0.0, 1.0, 0.0, 0.0,
            -s,  0.0, c,   0.0,
            0.0, 0.0, 0.0, 1.0
        );
    }

    public static function rotationZ(float radians): Matrix4f {
        float c = cos(radians);
        float s = sin(radians);
        return new Matrix4f(
            c,   -s,  0.0, 0.0,
            s,   c,   0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0
        );
    }

    // Rotation around arbitrary axis (axis must be normalized)
    public static function rotationAxis(Vec3f axis, float radians): Matrix4f {
        float c = cos(radians);
        float s = sin(radians);
        float t = 1.0 - c;

        float x = axis.x;
        float y = axis.y;
        float z = axis.z;

        return new Matrix4f(
            t * x * x + c,     t * x * y - s * z, t * x * z + s * y, 0.0,
            t * x * y + s * z, t * y * y + c,     t * y * z - s * x, 0.0,
            t * x * z - s * y, t * y * z + s * x, t * z * z + c,     0.0,
            0.0,               0.0,               0.0,               1.0
        );
    }

    // View matrix (look-at)
    public static function lookAt(Vec3f eye, Vec3f target, Vec3f up): Matrix4f {
        Vec3f zAxis = eye.subtract(target).normalize();
        Vec3f xAxis = up.cross(zAxis).normalize();
        Vec3f yAxis = zAxis.cross(xAxis);

        return new Matrix4f(
            xAxis.x, xAxis.y, xAxis.z, -xAxis.dot(eye),
            yAxis.x, yAxis.y, yAxis.z, -yAxis.dot(eye),
            zAxis.x, zAxis.y, zAxis.z, -zAxis.dot(eye),
            0.0,     0.0,     0.0,     1.0
        );
    }

    // Perspective projection matrix
    public static function perspective(float fovY, float aspect, float near, float far): Matrix4f {
        float tanHalfFov = tan(fovY / 2.0);
        float range = far - near;

        return new Matrix4f(
            1.0 / (aspect * tanHalfFov), 0.0,               0.0,                          0.0,
            0.0,                         1.0 / tanHalfFov,  0.0,                          0.0,
            0.0,                         0.0,               -(far + near) / range,        -2.0 * far * near / range,
            0.0,                         0.0,               -1.0,                         0.0
        );
    }

    // Orthographic projection matrix
    public static function orthographic(float left, float right, float bottom, float top, float near, float far): Matrix4f {
        float width = right - left;
        float height = top - bottom;
        float depth = far - near;

        return new Matrix4f(
            2.0 / width, 0.0,          0.0,          -(right + left) / width,
            0.0,         2.0 / height, 0.0,          -(top + bottom) / height,
            0.0,         0.0,          -2.0 / depth, -(far + near) / depth,
            0.0,         0.0,          0.0,          1.0
        );
    }
}
