// @SerializeField - Marker for editor-exposed script fields (VK-1458).
//
// Tag Behaviour/script fields the editor inspector should surface and
// persist. TODAY this is a forward-looking marker only: the engine's
// @Saveable JSON round-trip already persists all fields, and no inspector
// field-editing UI exists yet — when it lands, it keys on this annotation
// (readable at runtime via reflection thanks to @Retention(RUNTIME)).
//
// Usage:
//   @SerializeField
//   private PrefabRef bulletPrefab = new PrefabRef("assets/prefabs/bullet.vfPrefab");

@Retention(RUNTIME)
@Target([FIELD])
annotation SerializeField {
}
