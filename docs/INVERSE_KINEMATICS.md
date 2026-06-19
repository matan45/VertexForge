# Inverse Kinematics (IK)

Bend a bone chain so its **tip reaches a world-space target** — plant a foot on
uneven ground, make a hand grab a ledge, aim a head/look-at — *on top of* the
playing animation. VertexForge solves IK **at runtime as a post-process**: the
animator evaluates a pose first, then the IK solver bends the configured chains
toward their targets and blends the result back in by a per-chain weight.

The solver is a **generic FABRIK chain solver** (the same family Unity's
`TwoBoneIKConstraint` / Unreal's `FABRIK` node use). It is **not** a foot-planting
or look-at feature by itself — those are thin script helpers that *compute a target*
and feed it to the same generic solver.

> **Why you may never have seen IK do anything:** a chain only solves when it has
> an **active target**, and the target is **runtime state set from script**
> (`IK.setTarget(...)`). The editor lets you *configure* a chain (bones, weight,
> constraints) but there is **no target gizmo** in the inspector — so a freshly
> authored chain sits inert (`isActive = false`, `currentWeight = 0`) until a
> script drives it. See [§6 How to test it](#6-how-to-test-it).

---

## 1. Data model

IK is one ECS component, `IKTargetComponent`, holding N **chains**. Each chain
splits into **static config** (authored, serialized) and **runtime state**
(transient, set from script, never serialized).

| Type | Where | Lifetime |
|------|-------|----------|
| `IKChainConfig` | `utilities/animator/IKTypes.hpp` | Authored in editor, **serialized** into the scene |
| `IKChainRuntimeState` | `utilities/components/IKComponent.hpp` | Runtime only, **not serialized**, driven from script |
| `IKTargetComponent` | `utilities/components/IKComponent.hpp` | The component you add to an entity |

```cpp
// utilities/animator/IKTypes.hpp — authored config
struct IKChainConfig {
    std::string chainName;                  // e.g. "LeftArm" — the handle scripts use
    std::string tipBoneName;                // end-effector bone (e.g. hand)
    std::vector<std::string> chainBoneNames;// root → tip order
    std::vector<JointConstraint> constraints;// optional per-bone limits
    float weight = 1.0f;                    // max IK influence
    bool  enabled = true;
};

// utilities/components/IKComponent.hpp — runtime state
struct IKChainRuntimeState {
    std::vector<int32_t> resolvedBoneIndices; // bone names → skeleton indices (cached)
    int32_t  resolvedTipIndex = -1;
    glm::vec3 targetPosition{0.0f};           // WORLD space
    std::optional<glm::quat> targetRotation;  // optional tip orientation
    float currentWeight = 0.0f;               // runtime blend (0 = off)
    bool  isActive = false;                   // off until a target is set
};
```

Final per-chain influence is `config.weight * state.currentWeight`, clamped to
`[0,1]` (`graphics/animation/IKPostProcess.cpp`). The skeleton/bone matrices come
from the mesh's skeleton — a chain needs **≥ 2 bones** and a valid tip to solve.

---

## 2. Authoring a chain (editor)

1. Select a **skeletal-mesh entity that has an Animator** (the skeleton supplies
   the bone list).
2. **Add Component → "Inverse Kinematics"** (`editor/windows/details/AddComponentPopup.cpp`,
   tooltip *"FABRIK IK solver for bone chain targeting (feet, hands, look-at)"*).
3. In the **Inverse Kinematics** inspector section (`editor/windows/details/IKDrawer.cpp`):
   - **Chain name** — the string scripts reference (e.g. `LeftArm`, `LeftFoot`).
   - **Tip bone** — dropdown of skeleton bones (the end-effector).
   - **Chain bones** — ordered root→tip list (add/remove rows).
   - **Weight** (0–1) and **Enabled**.
   - **Constraints** per bone — `None` / `Hinge` / `Cone` / `BallAndSocket`.

Chains are **serialized** into the `.vfScene`
(`utilities/serialization/SceneSerializeIK.cpp`), so the configuration persists.
The runtime state (target/weight/active) is **not** saved.

> The inspector configures the chain only. It does **not** set a target or
> activate the chain — that is the script's job (next section).

---

## 3. Driving IK at runtime (mType)

Targets and activation come from the `IK` script API
(`assets/scripts/lib/engine/IK.mt`, backed by `core/adapters/api/IKAPI.cpp` →
`core/adapters/physics/IKAdapter.cpp`):

```mtype
int self = Entity::self();
if (IK::hasComponent(self)) {
    // Set a world-space target — THIS activates the chain
    IK::setTarget(self, "LeftArm", new Vec3f(1.0, 2.0, 3.0));

    // Optionally pin the tip orientation too
    IK::setTargetWithRotation(self, "RightArm", targetPos, Quaternion::identity());

    // Blend in/out (0 = animation only, 1 = full IK)
    IK::setChainWeight(self, "LeftArm", 0.75);

    // Toggle a chain
    IK::setChainEnabled(self, "LeftArm", true);

    // Queries
    string[] names = IK::getChainNames(self);
    float w        = IK::getChainWeight(self, "LeftArm");
    bool on        = IK::isChainEnabled(self, "LeftArm");
}
```

**Activation flow** — calling `IK.setTarget(...)` is what turns a chain on
(`IKAdapter::setTarget`, `core/adapters/physics/IKAdapter.cpp`):

```cpp
state.targetPosition = position;
state.targetRotation = rotation;
state.isActive       = true;            // ← chain now eligible to solve
state.currentWeight  = chains[i].weight;// ← inherits the authored weight
```

So: **no `setTarget` → no IK.** To stop IK, set the weight to 0 (or fade it for a
smooth blend out) or disable the chain.

### Helper libraries (compute-the-target wrappers)

These don't add new solver capability — they do the math to produce a target,
then call `IK.setTarget`:

| Helper | File | What it computes |
|--------|------|------------------|
| **Foot IK** | `assets/scripts/lib/engine/FootIK.mt` | Raycasts to ground, foot placement + pelvis offset |
| **Hand IK** | `assets/scripts/lib/engine/HandIK.mt` | Reach/clamp toward a grab point, two-handed grip |
| **Look-At** | (helper math, see `tests/test_ik.cpp`) | Aim within a max angle, dead zone, smoothing |

Foot-planting, look-at and reach are therefore **conventions on top of the
generic solver**, not engine-side features. Auto-targeting (raycasts, etc.) lives
in script, not in the C++ solver.

---

## 4. When IK runs

IK is a post-process inside the animation system
(`graphics/animation/RuntimeAnimatorSystem.cpp`), after the pose is evaluated:

- **Play mode** — `updateAll()` → per active animator: `applyIKPostProcess(...)`.
- **Edit mode** — `updateEditModePreview()` (VK-1407) → per animator:
  `evaluateRestPose()` then `applyIKPostProcess(...)`.

`applyIKPostProcess` (`graphics/animation/IKPostProcess.cpp`) loads the entity's
skeleton, then for each chain **solves only if**:

```
state.isActive  &&  config.enabled  &&  state.currentWeight > 0
```

For each solving chain it converts the relevant bone matrices to **world space**,
runs FABRIK, then writes the blended result back into the bone (skinning) matrices
that get uploaded to the GPU.

> **Edit-mode caveat (VK-1407):** the edit preview *calls* `applyIKPostProcess`,
> but because targets are runtime/script state and scripts don't run in edit mode,
> chains are normally inert there — so IK is effectively a no-op in the editor
> preview unless a target was set. The rest-pose preview still works; you just
> won't see IK bending until Play. (A future editor target gizmo would close this.)

---

## 5. The solver (FABRIK)

`graphics/animation/IKSolver.cpp` — **F**orward **A**nd **B**ackward **R**eaching
**I**nverse **K**inematics, an iterative position solver:

1. **Reachability** — if the target is farther than the summed bone lengths, the
   chain is stretched straight at the target.
2. **Iterate** up to `maxIterations` (default **10**), stopping when the tip is
   within `tolerance` (default **0.001**):
   - **Backward pass** — pin the tip to the target, walk to the root keeping each
     bone length.
   - **Forward pass** — pin the root back to its original position, walk to the
     tip keeping bone lengths.
   - **Apply constraints** (if any) after each iteration.
3. **Rotations** — derive each joint quaternion from the change in bone direction;
   if a `targetRotation` was supplied, force it onto the tip.

**Joint constraints** (`utilities/animator/IKTypes.hpp`, applied in
`IKSolver.cpp`):

| Type | Use | Parameters |
|------|-----|-----------|
| `Hinge` | elbow/knee (1 axis) | `hingeAxis` |
| `Cone` | symmetric swing limit | `coneAngle` |
| `BallAndSocket` | swing + twist limits | `swingAngle`, `twistMin`, `twistMax` |

**Not supported today:** pole/hint vectors (FABRIK resolves a 2-bone chain to the
target but you can't steer the elbow/knee direction), and any engine-side
auto-targeting.

---

## 6. How to test it

The fastest end-to-end check (Play mode, because targets are script-driven):

1. **Pick a skeletal character** with an animator (e.g. the RTSDemo soldier).
2. **Add Component → Inverse Kinematics**, add a chain — e.g. a left-arm chain:
   - Chain name: `LeftArm`
   - Chain bones (root→tip): `LeftUpperArm`, `LeftLowerArm`
   - Tip bone: `LeftHand`
   - Weight: `1.0`, Enabled: ✔
   - (Optional) set `LeftLowerArm` constraint to `Hinge` for a natural elbow.
3. **Attach a small mType script** to the entity that sets a target every frame —
   start with a fixed point so you can see the arm snap to it:

   ```mtype
   @Script
   public class IKTest {
       public function onUpdate(float dt): void {
           int self = Entity::self();
           if (IK::hasComponent(self)) {
               // a point in front of / above the character (world space)
               IK::setTarget(self, "LeftArm", new Vec3f(0.0, 1.5, 1.0));
               IK::setChainWeight(self, "LeftArm", 1.0);
           }
       }
   }
   ```
4. **Build Scripts**, then **Play**. The left hand should pull toward
   `(0, 1.5, 1)`. Animate the target (e.g. orbit it, or use another entity's
   position) and the arm tracks it. Drop the weight to `0.0` and it returns to the
   pure animation pose — that's the blend working.

**Sanity-test the math without the editor:** `bin/Tests/<Config>/x64/Tests.exe
--source-file="*test_ik*"` runs the FootIK/HandIK/LookAt helper tests
(`tests/test_ik.cpp`). Note these cover the **target-computing helpers**, not the
full component→solver path (there is no end-to-end component test yet).

---

## 7. File map

| Area | File | Module/DLL |
|------|------|-----------|
| Config types + constraints | `utilities/animator/IKTypes.hpp` | Utilities (header) |
| Component + runtime state | `utilities/components/IKComponent.hpp` | Utilities (header) |
| FABRIK solver | `graphics/animation/IKSolver.{hpp,cpp}` | Animation DLL |
| Post-process / gating / space conversion | `graphics/animation/IKPostProcess.cpp` | Animation DLL |
| Evaluation hooks | `graphics/animation/RuntimeAnimatorSystem.cpp` (`updateAll`, `updateEditModePreview`) | Animation DLL |
| Editor inspector | `editor/windows/details/IKDrawer.{hpp,cpp}` | Editor |
| Add-component entry | `editor/windows/details/AddComponentPopup.cpp` | Editor |
| Script API | `assets/scripts/lib/engine/IK.mt` | mType assets |
| Natives | `core/adapters/api/IKAPI.cpp` → `core/adapters/physics/IKAdapter.cpp` | Core |
| Service/commands | `services/impl/components/IKComponentService.cpp`, `services/events/physics/IKEvents.hpp` | Services |
| Serialization | `utilities/serialization/SceneSerializeIK.cpp` | Serialization DLL |
| Helper scripts | `assets/scripts/lib/engine/{FootIK,HandIK}.mt` | mType assets |
| Tests (helpers only) | `tests/test_ik.cpp` | Tests |

---

## 8. Limitations / gotchas

- **No target without script** — chains are inert until `IK.setTarget` is called;
  the editor has no target gizmo.
- **No pole/hint vector** — you can't control the elbow/knee swing direction yet.
- **No built-in foot-planting/look-at** — those are script helpers that compute a
  target; the C++ solver is target-agnostic.
- **≥ 2 bones per chain**, tip must resolve, or the chain is skipped silently.
- **World-space targets** — `setTarget` positions are world space, not local.
- **Edit-mode preview** runs the IK pass but won't show IK bending (no live
  targets in edit mode) — see [§4](#4-when-ik-runs).
