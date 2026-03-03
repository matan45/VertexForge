import * from "../math/Vec3f.mt";
import * from "../math/Vec4f.mt";

public class DebugDraw {

    public static function line(Vec3f start, Vec3f end, Vec4f color) {
        _native_debugDraw_line(
            start.x, start.y, start.z,
            end.x, end.y, end.z,
            color.x, color.y, color.z, color.w);
    }

    public static function ray(Vec3f origin, Vec3f direction, float length, Vec4f color) {
        _native_debugDraw_ray(
            origin.x, origin.y, origin.z,
            direction.x, direction.y, direction.z,
            length,
            color.x, color.y, color.z, color.w);
    }

    public static function box(Vec3f center, Vec3f halfExtents, Vec4f color) {
        _native_debugDraw_box(
            center.x, center.y, center.z,
            halfExtents.x, halfExtents.y, halfExtents.z,
            color.x, color.y, color.z, color.w);
    }

    public static function sphere(Vec3f center, float radius, Vec4f color) {
        _native_debugDraw_sphere(
            center.x, center.y, center.z,
            radius,
            color.x, color.y, color.z, color.w);
    }

    public static function setEnabled(bool enabled) {
        _native_debugDraw_setEnabled(enabled);
    }

    public static function isEnabled() : bool {
        return _native_debugDraw_isEnabled();
    }
}
