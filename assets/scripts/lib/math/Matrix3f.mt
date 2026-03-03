// Matrix3f - 3x3 floating-point matrix
// Row-major order: m[row][col] = data[row * 3 + col]
// Used for 2D transformations, rotations, and normal transformations

public value class Matrix3f {
    // Matrix elements stored in row-major order
    // | m00 m01 m02 |
    // | m10 m11 m12 |
    // | m20 m21 m22 |
    public float m00; public float m01; public float m02;
    public float m10; public float m11; public float m12;
    public float m20; public float m21; public float m22;

    // Constructors
    public constructor() {
        // Initialize to identity
        this.m00 = 1.0; this.m01 = 0.0; this.m02 = 0.0;
        this.m10 = 0.0; this.m11 = 1.0; this.m12 = 0.0;
        this.m20 = 0.0; this.m21 = 0.0; this.m22 = 1.0;
    }

    public constructor(float m00, float m01, float m02,
                       float m10, float m11, float m12,
                       float m20, float m21, float m22) {
        this.m00 = m00; this.m01 = m01; this.m02 = m02;
        this.m10 = m10; this.m11 = m11; this.m12 = m12;
        this.m20 = m20; this.m21 = m21; this.m22 = m22;
    }

    // Matrix operations
    public function add(Matrix3f other): Matrix3f {
        return new Matrix3f(
            this.m00 + other.m00, this.m01 + other.m01, this.m02 + other.m02,
            this.m10 + other.m10, this.m11 + other.m11, this.m12 + other.m12,
            this.m20 + other.m20, this.m21 + other.m21, this.m22 + other.m22
        );
    }

    public function subtract(Matrix3f other): Matrix3f {
        return new Matrix3f(
            this.m00 - other.m00, this.m01 - other.m01, this.m02 - other.m02,
            this.m10 - other.m10, this.m11 - other.m11, this.m12 - other.m12,
            this.m20 - other.m20, this.m21 - other.m21, this.m22 - other.m22
        );
    }

    public function multiply(float scalar): Matrix3f {
        return new Matrix3f(
            this.m00 * scalar, this.m01 * scalar, this.m02 * scalar,
            this.m10 * scalar, this.m11 * scalar, this.m12 * scalar,
            this.m20 * scalar, this.m21 * scalar, this.m22 * scalar
        );
    }

    public function multiply(Matrix3f other): Matrix3f {
        return new Matrix3f(
            this.m00 * other.m00 + this.m01 * other.m10 + this.m02 * other.m20,
            this.m00 * other.m01 + this.m01 * other.m11 + this.m02 * other.m21,
            this.m00 * other.m02 + this.m01 * other.m12 + this.m02 * other.m22,

            this.m10 * other.m00 + this.m11 * other.m10 + this.m12 * other.m20,
            this.m10 * other.m01 + this.m11 * other.m11 + this.m12 * other.m21,
            this.m10 * other.m02 + this.m11 * other.m12 + this.m12 * other.m22,

            this.m20 * other.m00 + this.m21 * other.m10 + this.m22 * other.m20,
            this.m20 * other.m01 + this.m21 * other.m11 + this.m22 * other.m21,
            this.m20 * other.m02 + this.m21 * other.m12 + this.m22 * other.m22
        );
    }

    // Transform a Vec3f
    public function transform(Vec3f v): Vec3f {
        return new Vec3f(
            this.m00 * v.x + this.m01 * v.y + this.m02 * v.z,
            this.m10 * v.x + this.m11 * v.y + this.m12 * v.z,
            this.m20 * v.x + this.m21 * v.y + this.m22 * v.z
        );
    }

    // Transform a Vec2f (assumes z = 1 for 2D homogeneous coordinates)
    public function transform2D(Vec2f v): Vec2f {
        float w = this.m20 * v.x + this.m21 * v.y + this.m22;
        if (w != 0.0) {
            return new Vec2f(
                (this.m00 * v.x + this.m01 * v.y + this.m02) / w,
                (this.m10 * v.x + this.m11 * v.y + this.m12) / w
            );
        }
        return new Vec2f(
            this.m00 * v.x + this.m01 * v.y + this.m02,
            this.m10 * v.x + this.m11 * v.y + this.m12
        );
    }

    public function transpose(): Matrix3f {
        return new Matrix3f(
            this.m00, this.m10, this.m20,
            this.m01, this.m11, this.m21,
            this.m02, this.m12, this.m22
        );
    }

    public function determinant(): float {
        return this.m00 * (this.m11 * this.m22 - this.m12 * this.m21)
             - this.m01 * (this.m10 * this.m22 - this.m12 * this.m20)
             + this.m02 * (this.m10 * this.m21 - this.m11 * this.m20);
    }

    public function inverse(): Matrix3f {
        float det = this.determinant();
        if (det == 0.0) {
            return Matrix3f.identity();
        }

        float invDet = 1.0 / det;

        return new Matrix3f(
            (this.m11 * this.m22 - this.m12 * this.m21) * invDet,
            (this.m02 * this.m21 - this.m01 * this.m22) * invDet,
            (this.m01 * this.m12 - this.m02 * this.m11) * invDet,

            (this.m12 * this.m20 - this.m10 * this.m22) * invDet,
            (this.m00 * this.m22 - this.m02 * this.m20) * invDet,
            (this.m02 * this.m10 - this.m00 * this.m12) * invDet,

            (this.m10 * this.m21 - this.m11 * this.m20) * invDet,
            (this.m01 * this.m20 - this.m00 * this.m21) * invDet,
            (this.m00 * this.m11 - this.m01 * this.m10) * invDet
        );
    }

    // Comparison
    public function equals(Matrix3f other): bool {
        return this.m00 == other.m00 && this.m01 == other.m01 && this.m02 == other.m02 &&
               this.m10 == other.m10 && this.m11 == other.m11 && this.m12 == other.m12 &&
               this.m20 == other.m20 && this.m21 == other.m21 && this.m22 == other.m22;
    }

    // Utility
    public function toString(): string {
        return "Matrix3f[\n" +
               "  " + parsePrimitive(this.m00) + ", " + parsePrimitive(this.m01) + ", " + parsePrimitive(this.m02) + "\n" +
               "  " + parsePrimitive(this.m10) + ", " + parsePrimitive(this.m11) + ", " + parsePrimitive(this.m12) + "\n" +
               "  " + parsePrimitive(this.m20) + ", " + parsePrimitive(this.m21) + ", " + parsePrimitive(this.m22) + "\n" +
               "]";
    }

    // Static factory methods
    public static function identity(): Matrix3f {
        return new Matrix3f();
    }

    public static function zero(): Matrix3f {
        return new Matrix3f(
            0.0, 0.0, 0.0,
            0.0, 0.0, 0.0,
            0.0, 0.0, 0.0
        );
    }

    // 2D transformation matrices (homogeneous coordinates)
    public static function translation2D(float tx, float ty): Matrix3f {
        return new Matrix3f(
            1.0, 0.0, tx,
            0.0, 1.0, ty,
            0.0, 0.0, 1.0
        );
    }

    public static function scale2D(float sx, float sy): Matrix3f {
        return new Matrix3f(
            sx,  0.0, 0.0,
            0.0, sy,  0.0,
            0.0, 0.0, 1.0
        );
    }

    public static function rotation2D(float radians): Matrix3f {
        float c = cos(radians);
        float s = sin(radians);
        return new Matrix3f(
            c,   -s,  0.0,
            s,   c,   0.0,
            0.0, 0.0, 1.0
        );
    }

    // 3D rotation matrices (for rotating Vec3f)
    public static function rotationX(float radians): Matrix3f {
        float c = cos(radians);
        float s = sin(radians);
        return new Matrix3f(
            1.0, 0.0, 0.0,
            0.0, c,   -s,
            0.0, s,   c
        );
    }

    public static function rotationY(float radians): Matrix3f {
        float c = cos(radians);
        float s = sin(radians);
        return new Matrix3f(
            c,   0.0, s,
            0.0, 1.0, 0.0,
            -s,  0.0, c
        );
    }

    public static function rotationZ(float radians): Matrix3f {
        float c = cos(radians);
        float s = sin(radians);
        return new Matrix3f(
            c,   -s,  0.0,
            s,   c,   0.0,
            0.0, 0.0, 1.0
        );
    }

    public static function scale(float sx, float sy, float sz): Matrix3f {
        return new Matrix3f(
            sx,  0.0, 0.0,
            0.0, sy,  0.0,
            0.0, 0.0, sz
        );
    }
}
