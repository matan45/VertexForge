# Ragdoll / Physics-Animation Validation (VK-1440)

Manual, in-editor QA checklist for the VertexForge **ragdoll / physics-animation**
pipeline. The pipeline lets a skeletal character hand its bone transforms over to
the physics solver (full ragdoll), or have joint motors drive those bones toward
the playing animation (powered ragdoll), then blend back to ordinary animation —
all at runtime.

This document covers the behavior that **cannot be unit-tested**: it needs the
built Editor, a GPU, **Play** mode, and a real rigged character. The
CPU-deterministic parts are covered by the headless suite (see §5).

> **Scope — skeletal meshes only.** Physics animation works **only** on a rigged,
> animated (skinned) `.vfMesh`. It never applies to a static mesh. The validation
> character **must** be a rigged humanoid with an animator and at least one
> **moving** animation clip (a walk/run/idle-with-motion — not a frozen bind
> pose). Several checks below depend on the character actually animating when you
> enter and leave ragdoll.

The four physics-animation modes (`PhysicsAnimationMode`) are:

| Mode | Behavior |
|------|----------|
| **Animated** | Normal — the animator drives the bones; no physics bodies. |
| **Kinematic** | Bodies follow the animated pose and push other physics objects, but the pose still comes from the animator. |
| **Ragdoll** | Physics fully owns the bones; the animator stops fighting it. |
| **PoweredRagdoll** | Bodies stay active, but joint motors drive each bone *toward* the playing animation — physically-driven animation that resists and recovers. |

---

## 1. Prerequisites

1. **A rigged humanoid `.vfMesh`** with a skeleton, an assigned `.vfAnimator`,
   and at least one **moving** clip (so the body has a live pose to react from).
2. **A bound `.vfPhysAnim`.** A checked-in preset lives at
   `assets/physicsAnimation/humanoid_standard.vfPhysAnim`. It maps physics bodies
   to skeleton bones **by name**, so the bone names in the preset must match the
   test character's skeleton — see §2 if your rig uses different names.
3. **Build the Editor in Development:**
   ```bash
   premake5 vs2022
   msbuild VFEngine/VertexForge.sln /t:Editor /p:Configuration=Development /p:Platform=x64
   ```
4. **Attach the component.** Select the character entity and add a
   `PhysicsAnimationComponent`, pointing it at the `.vfPhysAnim` asset.
5. **Enter Play** to exercise runtime behavior (`F5`, or the toolbar Play button).
   Most checks below are Play-mode only; ragdoll, motors, impulses, hit reactions,
   and the GPU override path do not run in edit mode.

> The provided `RagdollController.mt` (§AC8) is the quickest harness: attach it
> to the character, enter Play, and use **R / P / T / H** to drive the modes.

---

## 2. Bone naming

The `.vfPhysAnim` preset maps each physics body to a skeleton bone **by name**.
There is **no universal ragdoll bone-name standard** — Unity, UE5, and Godot all
key off the imported skeleton's *own* bone names. The preset therefore uses the
convergent **Standard Humanoid** names (Unity-Humanoid ≈ Godot
`SkeletonProfileHumanoid` ≈ glTF/VRM), and must be **rebound to match the actual
rig** if the character's skeleton uses other names (e.g. a Mixamo import).

| Body part | Standard (preset) | Mixamo |
|-----------|-------------------|--------|
| Hips | `Hips` | `mixamorig:Hips` |
| Spine | `Spine` | `mixamorig:Spine` |
| Chest | `Chest` | `mixamorig:Spine1` or `mixamorig:Spine2` |
| Neck | `Neck` | `mixamorig:Neck` |
| Head | `Head` | `mixamorig:Head` |
| Left shoulder | `LeftShoulder` | `mixamorig:LeftShoulder` |
| Left upper arm | `LeftUpperArm` | `mixamorig:LeftArm` |
| Left lower arm | `LeftLowerArm` | `mixamorig:LeftForeArm` |
| Left hand | `LeftHand` | `mixamorig:LeftHand` |
| Left upper leg | `LeftUpperLeg` | `mixamorig:LeftUpLeg` |
| Left lower leg | `LeftLowerLeg` | `mixamorig:LeftLeg` |
| Left foot | `LeftFoot` | `mixamorig:LeftFoot` |

The **Right** side mirrors the Left (`RightShoulder` / `mixamorig:RightShoulder`,
`RightUpperArm` / `mixamorig:RightArm`, and so on).

> If your rig differs, either rename the bones to the Standard names, or edit the
> `boneName` fields in the Animation Physics panel (§AC10) so every body, joint
> limit, and motor in the preset points at a bone that exists on the skeleton. A
> body whose `boneName` doesn't resolve simply has no effect.

---

## 3. Validation checklist

Each subsection is numbered to match the VK-1440 acceptance criteria (AC #3–#12).
Work through them in Play mode with the validation character selected.

### AC3 — Animated → Ragdoll from a *moving* animation
- [ ] Start in **Animated** mode with a moving clip playing (the character is
      mid-stride / mid-motion, not in bind pose).
- [ ] Switch to **Ragdoll** (R in the demo script, or the runtime mode control in
      the details panel).
- [ ] **Expected:** the bodies initialize from the *current animated pose* — there
      is **no visible snap** to bind pose or T-pose, and the animator does **not**
      fight physics once ragdoll takes over. The character collapses naturally
      from wherever the limbs were.

### AC4 — PoweredRagdoll tracks the animation
- [ ] From Animated (clip still playing), switch to **PoweredRagdoll** (P in the
      demo script, or set mode `PoweredRagdoll`).
- [ ] **Expected:** the bodies stay active while joint motors drive each bone
      *toward* the playing animation — the character keeps animating, but now
      physically.
- [ ] Push / shove the character (a per-bone impulse, or a moving physics object).
      **Expected:** it **resists**, gets deflected, and **recovers** back toward
      the animated pose rather than going limp or freezing.

### AC5 — PoweredRagdoll / Ragdoll → Animated (blend-out)
- [ ] From a ragdoll mode, switch back to **Animated** (T in the demo script, or
      set mode `Animated`).
- [ ] **Expected:** a **smooth blend-out** through the captured ragdoll pose into
      the animator — no instantaneous pop. The runtime override state clears and
      the mesh follows the animator again (verify the clip resumes driving it).

### AC6 — Impulses (whole-body and per-bone)
- [ ] **Whole-body:** apply a whole-body impulse (R in the demo activates ragdoll
      *with* a knockback impulse; or call the whole-body impulse API on an already
      active ragdoll). **Expected:** the whole ragdoll is visibly launched in the
      impulse direction.
- [ ] **Per-bone:** apply a per-bone impulse (per-bone impulse API, by bone
      index). **Expected:** that body and its chain react locally — e.g. an arm or
      head is yanked while the rest trails.
- [ ] Trigger impulses **both** ways: via the **script API** and via the
      **details-panel / runtime controls**, and confirm both paths move the body.

### AC7 — Hit reaction
- [ ] Trigger a hit reaction (H in the demo hits the **Chest** bone, or the
      **Test Hit Reaction** button in the entity details panel).
- [ ] **Expected:** a **directional impulse** plus a temporary **motor-strength
      dip** on the targeted bone and its descendant chain — the character flinches
      / staggers on impact.
- [ ] **Expected:** the chain **recovers** back to full tracking over the
      configured recover time (the preset's `hitReaction.defaultRecoverTime`),
      not staying limp.

### AC8 — Ragdoll lifecycle events
- [ ] Attach `assets/scripts/game/RagdollController.mt` (which implements
      `IRagdollListener`) to the character and enter **Play**.
- [ ] Drive it through ragdoll and back (R → wait for settle, or R then T).
- [ ] **Expected (watch the console log):**
  - `onRagdollActivated` fires when entering Ragdoll / PoweredRagdoll,
  - `onRagdollSettled` fires once the bodies have been near-still for the
    configured settle window, and
  - `onRagdollDeactivated` fires when returning to Animated / Kinematic.
- [ ] Confirm all three fire in real Play mode (the demo logs a line for each).

### AC9 — GPU override path
- [ ] While **Ragdoll** or **PoweredRagdoll** is active, watch the mesh deform.
      **Expected:** the skin deforms with the *physics* pose — the renderer is
      uploading `PhysicsAnimationComponent.overrideBoneMatrices` instead of the
      animator matrices.
- [ ] **Deactivate** (back to Animated). **Expected:** the mesh returns to the
      animator matrices and resumes the clip cleanly.
- [ ] Verify visually that there is **no T-pose / frozen / collapsed frame** at
      either the activation *or* the deactivation boundary.

### AC10 — Editor asset round-trip (`.vfPhysAnim`)
- [ ] Open the **Animation Physics panel** and **Load** the
      `humanoid_standard.vfPhysAnim` preset.
- [ ] Edit a few fields — e.g. one body's **mass**, one **joint limit** (a swing
      or twist angle), and one **motor** (strength / max torque).
- [ ] Click **Save**, then **Save As…** a copy under a new name.
- [ ] Reload the saved file (and the copy). **Expected:** all of the following are
      preserved exactly: body→bone mappings, body shapes/sizes/masses, joint
      limits, motors, hit-reaction settings, and settle thresholds.

### AC11 — Collider overlay
- [ ] In the Animation Physics panel toggle **Show Colliders** on.
- [ ] **Expected:** the wire bodies (capsules / spheres / boxes) draw on the
      skeleton and **track the selected bones** closely — close enough to author
      and tune body sizes and offsets against the actual mesh.
- [ ] Select different bones / bodies and confirm the highlighted body follows the
      corresponding bone.

### AC12 — Serialization (scene + prefab)
- [ ] Place the configured character **in a scene**, save, reload the scene.
      **Expected:** the `PhysicsAnimationComponent` survives — its `.vfPhysAnim`
      asset reference *and* the embedded config are intact.
- [ ] Save the character **as a `.vfPrefab`**, then instantiate it into a scene
      and reload. **Expected:** same — asset ref + embedded config survive.
- [ ] **Play → Stop** with the character present. **Expected:** runtime-only state
      (current mode, active ragdoll bodies, motor-strength overrides, settle
      counters) **resets correctly** on Stop — the character is back in its
      authored default mode with no leftover physics state.

---

## 4. Self-explosion / stability troubleshooting

| Symptom | Likely cause / fix |
|---------|--------------------|
| Ragdoll **explodes / jitters violently** the instant it activates | Adjacent bodies are colliding. Check that parent–child collision is disabled between connected bodies, and that the collision filtering is correct. |
| Limbs **snap to extreme angles** or vibrate | A **joint limit is inverted** (min > max) or far too tight. Re-check the swing/twist angles for that bone in the panel. |
| Character **sinks into / clips through** the floor | Collider **sizes** are too small for the limbs, or the character is on the wrong **collision layer** — verify the body sizes and the `.vfPhysAnim` collision layer. |
| Powered ragdoll goes **fully limp** when it should track | Motor strength is zero (a leftover per-bone or global motor-strength override, or `defaultMotorStrength` = 0). Reset motor strength. |
| Powered ragdoll is **stiff / can't be pushed** | Motors are over-strong / max torque too high — lower motor strength or max torque. |

> Tune body shapes, sizes, offsets, joint limits, and motors in the **Animation
> Physics panel** with **Show Colliders** on (§AC11), then re-validate the runtime
> checks above.

---

## 5. Coverage note

The CPU-deterministic parts of this pipeline — config parse/serialize round-trips,
bone-name mapping, mode-state transitions, and the like — are covered by the
headless suite in `VFEngine/tests/test_physics_animation.cpp`:

```bash
bin/Tests/Debug/x64/Tests.exe --source-file="*test_physics_animation*"
```

The manual checks in this document cover the **runtime, GPU, and editor** behavior
that the headless test suite cannot reach: live physics simulation, the GPU bone
override path, blend-in/blend-out feel, the collider overlay, lifecycle events in
real Play mode, and scene/prefab serialization round-trips.
