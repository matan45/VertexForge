# Weapon Hand IK — keep the support hand on a two-handed weapon

When a character holds a two-handed weapon (rifle, bow, spear), the weapon is
attached to **one** socket on the dominant hand and rigidly follows that hand's
bone. The **support (off) hand** is whatever the playing animation poses it to —
so it only touches the weapon in clips that were authored gripping it. In any
other clip (a generic run, a turn, a reload) the support hand **drifts off the
weapon**.

A socket offset can't fix this: it is one static transform
(`SocketDefinition.localPosition`/`localRotation`,
`utilities/animator/SocketTypes.hpp`) baked into the `.vfMesh`, so tuning it moves
the weapon *uniformly* in every state — it can never make the off-hand track the
weapon per-pose.

This guide shows the fix used by UE5 and Unity, mapped onto VertexForge's existing
socket + IK systems. **No engine code is required** — it's authoring plus a few
lines of script.

> Companion reference: [INVERSE_KINEMATICS.md](INVERSE_KINEMATICS.md) (the full IK
> data model, solver, and script API).

---

## The pattern (how UE5 / Unity do it)

Both engines use the same two parts, and it is **not** per-animation socket offsets:

1. **Weapon → dominant-hand socket** (e.g. `Weapon_R` on the right hand). It
   follows that bone.
2. **Off-hand → grip point on the weapon, via IK**, every frame, *after* the base
   animation is evaluated:
   - **UE5:** a `FABRIK` / `Two Bone IK` node pulls the left hand to a socket
     placed on the weapon mesh.
   - **Unity:** an Animation Rigging `Two Bone IK Constraint` (or `OnAnimatorIK` +
     `SetIKPosition`) targets a grip transform on the weapon.

The grip point lives **on the weapon**, so it is correct regardless of which clip
plays — idle, walk, run, fire all keep the support hand planted.

VertexForge mirrors this exactly: the weapon attaches via a **socket**, and the
off-hand is driven by the **FABRIK IK post-process** toward a **grip socket on the
weapon**.

---

## The three pieces

| Piece | Lives on | Type | Authored in |
|-------|----------|------|-------------|
| Dominant-hand socket (e.g. `Weapon_R`) | character mesh (skeletal, a hand bone) | **animation** (bone) socket | Animation Preview (`AnimationSocketPanel`) |
| Grip socket (e.g. `LeftHandGrip`) | weapon mesh (static) | **static** socket | Mesh Preview (`MeshPreviewWindow`) |
| IK chain (e.g. `LeftArm`) | character entity | `IKTargetComponent` | Details inspector (`IKDrawer`) → serialized |

A static socket on the weapon still tracks the moving hand: its world transform is
`parentWorld * localOffset`, the weapon's `parentWorld` is driven by the dominant
hand bone, and the engine resolves the chain *hand → weapon → grip* in one frame
(see `assets/scripts/lib/engine/Socket.mt`).

---

## Step by step

### 1. Get the dominant hand right first
Tune the weapon's hand socket (e.g. `Weapon_R`) in the Animation Preview so the
weapon sits naturally in the **dominant** hand at a good angle. IK only fixes the
*support* hand — the weapon pose itself must already look right.

### 2. Author a grip socket on the weapon
In **Mesh Preview** (`MeshPreviewWindow`), open the weapon's static mesh, add a
socket (e.g. `LeftHandGrip`) at the foregrip where the support hand should rest,
position/rotate it with the gizmo or the numeric fields, then **Save Sockets to
Mesh** (writes the `SOK2` block into the `.vfMesh`).

> Tip: use the **Orientation** buttons (X/Y/Z ±90, Reset) in the Mesh Preview panel
> to turn the weapon and place the grip from any angle. That rotation is
> preview-only — it never changes the saved mesh or the socket offsets.

### 3. Add an IK chain on the character
Select the character entity → **Add Component → Inverse Kinematics**
(`editor/windows/details/IKDrawer.cpp`). Add one chain:

- **Chain name** — the handle scripts use, e.g. `LeftArm`.
- **Chain bones (root→tip)** — the support arm's upper then lower arm bone.
- **Tip bone** — the support hand bone.
- **Constraint** on the lower-arm (elbow): `Hinge` for a natural bend (FABRIK has
  no pole vector, so the constraint is how you keep the elbow sane).
- **Weight** `1.0`, **Enabled** ✔.

Bone names are **rig-specific** — pick them from the dropdown. For a Mixamo rig the
left arm is:

```
chainBoneNames: ["mixamorig:LeftArm", "mixamorig:LeftForeArm"]
tipBoneName:    "mixamorig:LeftHand"
```

The chain is serialized into the scene/prefab
(`utilities/serialization/SceneSerializeIK.cpp`, key `"ikTarget"`), so it persists.
Constraints map **positionally** to the chain bones — to put a Hinge on the elbow
(index 1), the constraints array must be `[None, Hinge]`.

### 4. Drive the IK target from script, every frame
The chain is **inert until a script sets a target** — there is no editor target
gizmo. In the character's controller `onUpdate`, read the weapon's grip socket
world position and feed it to the chain
(`assets/scripts/lib/engine/{IK,Socket}.mt`):

```mtype
if (Socket::hasSocket(weaponId, "LeftHandGrip") && IK::hasComponent(self)) {
    Vec3f grip = Socket::getPosition(weaponId, "LeftHandGrip");
    IK::setTarget(self, "LeftArm", grip);          // world-space; activates the chain
    IK::setChainWeight(self, "LeftArm", 1.0);      // 0 = anim only, 1 = full IK
    // Wrist rolls oddly? pin orientation too:
    // IK::setTargetWithRotation(self, "LeftArm", grip, Socket::getRotation(weaponId, "LeftHandGrip"));
}
```

Guard with `hasSocket` / `hasComponent` so it's a graceful no-op until both are
authored.

---

## When it runs

IK is a post-process inside the animation system
(`graphics/animation/RuntimeAnimatorSystem.cpp` → `IKPostProcess.cpp`): the
animator evaluates the pose, then the FABRIK solver bends the chain toward its
target and blends back by `config.weight * runtime.currentWeight`. Because it
layers on top of the playing clip, the support hand stays on the weapon in **every
state**.

**Visible in Play only** — targets are runtime/script state and scripts don't run
in edit mode, so the chain stays inert in the editor preview even though the IK
pass executes there.

---

## Tuning

| Symptom | Fix |
|---------|-----|
| Hand on grip but **wrist rolls** | switch `IK::setTarget` → `IK::setTargetWithRotation` (pass the grip socket's rotation) |
| **Elbow bends the wrong way** | adjust the Hinge `hingeAxis` in the IK inspector, or set the elbow constraint to `None` (FABRIK still solves) |
| Want to **A/B test** the drift | set the chain weight to `0` — the hand returns to the raw animation pose |
| Chain does nothing | confirm the bone names match the rig exactly (a missing tip/bone logs a warning and silently skips the chain), and that a script is calling `setTarget` |

---

## Worked example — RTSDemo soldier

- **Weapon:** `ak-47A.vfMesh` attached to `Weapon_R` (right hand) in
  `SoldierCombatController.onStart`.
- **Grip socket:** `LeftHandGrip` authored on `ak-47A.vfMesh`.
- **IK chain:** `LeftArm` (`mixamorig:LeftArm` → `mixamorig:LeftForeArm`, tip
  `mixamorig:LeftHand`, Hinge elbow, weight 1) on `soldier_prefab.vfPrefab`.
- **Driver:** `SoldierCombatController.updateHandIK()` reads the grip socket and
  calls `IK::setTarget(self, "LeftArm", grip)` each frame.

After authoring the grip socket and running **Build Scripts**, press **Play** and
order a soldier to move — the left hand stays planted on the foregrip through
idle → run → fire.

---

## File map

| Area | File |
|------|------|
| Socket types / offset | `utilities/animator/SocketTypes.hpp` |
| Socket script API | `assets/scripts/lib/engine/Socket.mt` |
| IK component + runtime state | `utilities/components/IKComponent.hpp` |
| IK config + constraints | `utilities/animator/IKTypes.hpp` |
| IK script API | `assets/scripts/lib/engine/IK.mt` |
| IK solver / post-process | `graphics/animation/IKSolver.cpp`, `IKPostProcess.cpp` |
| IK chain serialization | `utilities/serialization/SceneSerializeIK.cpp` (key `"ikTarget"`) |
| Editor: IK chain authoring | `editor/windows/details/IKDrawer.cpp` |
| Editor: static socket authoring | `editor/windows/preview/MeshPreviewWindow.cpp` |
| Two-handed grip helper (alt.) | `assets/scripts/lib/engine/HandIK.mt` |
