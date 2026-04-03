public class Destruction {
    public static const int DAMAGE_ANY = 0;
    public static const int DAMAGE_EXPLOSIVE = 1;
    public static const int DAMAGE_BALLISTIC = 2;
    public static const int DAMAGE_MELEE = 3;

    public static function applyDamage(int entityId, float amount, int damageType, Vec3f impactPoint, Vec3f direction): void {
        _native_destruction_applyDamage(entityId, amount, damageType,
            impactPoint.x, impactPoint.y, impactPoint.z,
            direction.x, direction.y, direction.z);
    }

    public static function applySimpleDamage(int entityId, float amount): void {
        _native_destruction_applyDamage(entityId, amount);
    }

    public static function destroy(int entityId): void {
        _native_destruction_destroy(entityId);
    }

    public static function explode(Vec3f center, float radius, float damage, float force): void {
        _native_destruction_explode(center.x, center.y, center.z, radius, damage, force);
    }

    public static function getHealth(int entityId): float {
        return _native_destruction_getHealth(entityId);
    }

    public static function setHealth(int entityId, float hp): void {
        _native_destruction_setHealth(entityId, hp);
    }

    public static function isDestroyed(int entityId): bool {
        return _native_destruction_isDestroyed(entityId);
    }

    public static function repair(int entityId): void {
        _native_destruction_repair(entityId);
    }
}
