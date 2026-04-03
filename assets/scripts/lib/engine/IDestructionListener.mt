public interface IDestructionListener {
    function onDamageReceived(float amount, float remainingHealth, Vec3f impactPoint, Vec3f direction, int damageType): void;
    function onDestroyed(Vec3f impactPoint, Vec3f direction): void;
    function onFragmentCollision(int fragmentEntityId, int fragmentIndex): void;
}
