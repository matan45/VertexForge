# Animation Retargeting (VK-910)

Reuse one set of animations across characters with **different skeletons and
proportions** — author a walk/run/attack once and play it on any humanoid rig.
VertexForge retargets **at runtime**: the source clips are remapped onto the
target skeleton on the fly, so you never bake or duplicate animation per
character.

This is the same model as Unity's *Humanoid Avatar* and Unreal's *IK Retargeter*.

---

## 1. Concepts & assets

Retargeting flows through a shared, skeleton-agnostic set of **humanoid roles**
(`Hips`, `Spine`, `Head`, `LeftUpperArm`, `LeftLowerArm`, …):

```
source skeleton ──(source .vfrig)──► humanoid roles ──(target .vfrig)──► target skeleton
```

| Asset          | What it is | Where it's used |
|----------------|-----------|-----------------|
| **`.vfMesh`**  | A skinned mesh + its skeleton | Imported normally |
| **`.vfAnim`**  | An animation clip (authored for the **source** skeleton) | Referenced by a `.vfAnimator` |
| **`.vfrig`**   | **Humanoid Rig Profile** — maps *one* skeleton's bone names to humanoid roles, plus each bone's rest (T/A-pose) rotation | Referenced *inside* a `.vfretarget` (never assigned by hand) |
| **`.vfretarget`** | **Retarget binding** — references a *source* `.vfrig` + a *target* `.vfrig` (by GUID) + optional per-role overrides | Entity → **Mesh component → Retarget slot** |
| **`.vfAnimator`** | State machine / blend trees referencing `.vfAnim` clips | Entity → **Mesh component → Animator slot** |

**Key idea:** each character authors **one** `.vfrig`. The `.vfretarget` just
pairs two of them. So N characters need O(N) rig profiles, not O(N²) pairwise
maps — any character can be source *or* target in any combination.

> The `.vfAnimator` itself takes **neither** `.vfrig` nor `.vfretarget`. It only
> references `.vfAnim` clips. Retargeting is attached to the **entity**, not the
> animator.

---

## 2. Authoring a retarget (editor)

Open **Tools → Animation Retargeting** (or double-click an existing
`.vfretarget` / `.vfrig` in the Content Browser).

The window has two mapping columns (**Source** and **Target**) and a live 3D
preview on the right.

1. **Load meshes.** In each column click **Load Mesh…** and pick the character's
   `.vfMesh`. The skeleton appears as a 2D overlay.
2. **Auto-Map.** Click **Auto-Map** in each column. The name heuristic recognizes
   common conventions (Mixamo `mixamorig:LeftForeArm`, Unreal `lowerarm_l`,
   Blender `Arm.L`, 3ds Max `Bip01 L Forearm`). The status line shows
   *"All required roles mapped"* or how many are missing.
3. **Fix mismatches.** Required roles that are unmapped show in **red with a `*`**.
   Click a role row, then use the **Bone** dropdown to pick the correct bone.
   Selecting a role highlights its bone (cyan) in the 2D overlay.
   - Required roles: Hips, Spine, Head, both Upper/Lower Arms + Hands, both
     Upper/Lower Legs + Feet. Others (Chest, Neck, Shoulders, Toes, Eyes, Jaw)
     are optional and improve fidelity.
4. **Save.** Click **Save**. This writes:
   - a `.vfrig` next to the **source** mesh,
   - a `.vfrig` next to the **target** mesh,
   - the `.vfretarget` (you choose the location), plus `.vfmeta` sidecars.

### Live preview (optional, while authoring)
In the right **Retarget Preview** panel:
1. **Source Anim…** → pick a `.vfAnim` authored for the source character.
2. **Apply Retarget Preview** → loads the target mesh and plays the clip
   retargeted onto it. Use **Play / Pause**.
3. Edit role mappings and click **Apply Retarget Preview** again to see changes.

> Needs the target `.vfMesh` to have GPU-loadable skinned geometry. The 2D
> overlay always works for mapping even without the 3D preview.

---

## 3. Using a retarget at runtime

Retargeting is applied per **entity**, via its **Mesh component**:

1. Author one `.vfAnimator` that references the **source** character's `.vfAnim`
   clips — ordinary animator authoring.
2. Select the **target** entity. In the **Mesh** component:
   - **Animator** = that `.vfAnimator` (the one with source clips), and
   - **Retarget** = your `.vfretarget` (drag it onto the slot, or **Select
     Retarget**).
3. Enter **Play**. The animator plays the source clips; the `retargetRef` makes
   every clip retarget onto the target skeleton.

A single animator (built with source clips) can drive **any number** of
differently-proportioned characters — each just sets its own `retargetRef`.

> **When it applies:** changing the Retarget slot takes effect on the next
> **edit → play** transition (animators rebuild then). Set it in edit mode, then
> press Play. Clearing the slot returns the character to its native skeleton.

---

## 4. How the retarget math works (reference)

For each **target** bone that has a humanoid role:

- **Rotation** — the source bone's animated local rotation is converted into the
  target bone's local frame, relative to each skeleton's rest pose:
  `q_target = (q_target_rest · inverse(q_source_rest)) · q_source_anim`.
  At rest this reproduces the target's own bind pose exactly.
- **Translation** — only the **Hips** translate; their offset is scaled by the
  **leg-length ratio** (`targetLegLength / sourceLegLength`) so hip height tracks
  the target's size. All other bones keep their own bind translation, so limb
  lengths are never stretched — the target keeps its proportions.
- **Foot contact** — if the target skeleton has leg **IK chains**, the engine's
  IK post-process runs on the retargeted pose automatically, keeping feet planted.
- **Unmapped bones** (fingers, twist bones, props…) hold their bind pose.

The no-retarget path is unchanged: entities without a `retargetRef` evaluate
exactly as before.

---

## 5. Troubleshooting

| Symptom | Likely cause / fix |
|---------|--------------------|
| Bone **dropdowns** don't open in the editor | Use the freshly built editor (a fixed build). Relaunch. |
| `.vfrig` / `.vfretarget` show **no/blank icon** in the Content Browser | Relaunch the editor so the asset type is re-detected. |
| Limbs grossly **mis-rotated** | A role is mapped to the wrong bone — fix it in the role table (e.g. `LeftLowerArm` must be the forearm, not the hand). |
| Character **floats or sinks** | Leg roles (UpperLeg / LowerLeg / Foot) aren't mapped on one side, so the leg-length ratio is wrong. |
| Feet **slide** | Target skeleton has no leg IK chains — add them, or accept proportion-only planting. |
| Required role shows **red `*`** | Map it via the Bone dropdown; required roles must be set for a full-body clip. |
| Retarget has **no effect** at runtime | The `retargetRef` is set but you didn't re-enter Play; or the `.vfrig`/`.vfretarget` GUIDs didn't resolve (re-save from the editor so they register in the asset DB). |

---

## 6. Limitations

- Rotation uses a local **delta-from-rest** mapping — exact at rest, with minor
  error away from rest for skeletons whose bone twist axes differ a lot. A
  model-space variant is a possible future upgrade.
- Only the **Hips** retarget translation in v1 (the per-bone flag exists but is
  reserved).
- Retargeting is **runtime only** — there is no baked retargeted `.vfAnim`
  output (by design: a baked clip would be locked to a single target skeleton
  and couldn't be shared). The source clips stay reusable across all targets.
