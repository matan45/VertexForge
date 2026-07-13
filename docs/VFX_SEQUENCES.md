# VFX Combo Sequences (`.vfVFXSequence`)

Compose several existing `.vfVFX` effects into one **timed combo** and play it
standalone, from script, or automatically from an animation event. (VK-1425.)

> **Scope:** combos run in **editor Play mode, the sequence editor preview, and the
> shipped/exported Runtime** (`RuntimeHandler` constructs the same
> `VFXSequenceRuntimeServiceImpl` + `VFXSequencePlayModeHandler` the editor uses).

---

## 1. Concepts

| Term | What it is |
|---|---|
| **`.vfVFXSequence`** | The asset (a JSON file). An ordered list of **steps**; each step *references* an existing `.vfVFX` by path/GUID — no graph duplication. |
| **Step** | One `.vfVFX` placed in time (or behind a named cue), with an optional local transform, socket, and parameter overrides. |
| **Combo instance** | A live, playing copy of a sequence. One asset can be spawned many times → each spawn is a separate instance. |
| **`comboId`** | An integer handle to one running combo instance (returned by `VFX::spawnCombo`). `0` = failure. Used to control/stop/query that instance. |
| **Cue** | A named trigger *inside* a sequence. A cue-driven step does not fire on the timeline; it fires when something calls `triggerComboCue(name)`. |

A combo owns several child emitter instances internally and forwards
play/stop/reset/transform/destroy/socket to all of them — you only deal with the
single `comboId`.

---

## 2. Authoring in the editor

Create one via **Content Browser → right-click → Create ▸ VFX Sequence**, then
double-click it to open the **VFX Sequence Editor**.

- **Step list** (left) — add / remove / reorder steps.
- **Inspector** (middle) — edit the selected step (below).
- **Preview** (right) — the real GPU VFX of the **active** step, one at a time, as
  the timeline plays. (To see all steps composited, run the combo in Play mode.)
- **Timeline** (bottom) — an ImSequencer track view: **one draggable clip per step**.
  Drag a clip to set its **Start Time**; drag its **right edge** to set **Duration**.
  Pink clips are time-driven, gray are cue-driven. The playhead scrubs the preview;
  **Play** auto-advances, **Loop** wraps.

### Step fields

| Field | Meaning |
|---|---|
| **VFX Asset** | the child `.vfVFX` (drag one onto the field). |
| **Start Time** | *when* the step fires, in seconds from the combo's start. This is how you sequence: step A `0.0`, step B `0.5`, … |
| **Trigger** | **Time** (fires at Start Time), **Cue** (fired by `triggerComboCue`, uses Cue Name), or **Step Output** (spawns at a source step's particle-event location — see §6b). |
| **Cue Name** | the named cue this step waits for when **Trigger = Cue**. |
| **Local Position / Euler / Scale** | offset of this step relative to the combo's origin (or socket). |
| **Socket** | optional bone socket this step follows (see §4). |
| **Loop** | the child emitter loops while active. |
| **Duration** | `0` = play to completion. Otherwise how long it runs. |
| **Stop Mode** | `Play To Completion` or `Stop After Duration`. |
| **Scalar / Vector Overrides** | name-keyed overrides applied to the child emitter when spawned (e.g. `startColor`, `spawnRate`, `startSize`). |

---

## 3. Triggering a combo at runtime — three ways

### A. Data-driven (no script) — `VFXSequenceComponent`
Add a **VFX Sequence** component to an entity:

- **`sequenceRef` + `autoPlay`** → the combo plays when the entity enters Play mode
  (optionally attached to a `socketName`).
- **`loop`** → when set, the whole sequence **replays** each time it finishes: the
  timeline rewinds and the next tick re-spawns the steps and **re-fires the sounds,
  script cues, and event markers** (each marker fires exactly once per iteration). This
  is the "burning building" case — no babysitting script needed. Notes:
  - By default each loop **re-rolls** any per-step variety (`probability` / `variantGroup`,
    see §2) for variation; set the sequence's **Stable Loop** flag (asset setting) to
    replay the *identical* variant every iteration. Either way the loop is fully
    deterministic from the combo seed.
  - A sequence whose *steps* are themselves looping emitters (per-step **Loop**) never
    "finishes", so it is already continuous — whole-sequence replay simply never triggers.
  - **Off-screen looping combos pause** (children stopped + hidden, the tick frozen) and
    resume on re-entry — they are never destroyed. This whole-combo culling only kicks in
    when the sequence has **authored Fixed bounds** (use *Recalc Bounds* in the editor);
    with Auto/empty bounds the loop runs unculled. (Resume currently restarts a frozen
    emitter's emission — a small visual pop on ambient effects; a non-resetting resume is
    a tracked graphics follow-up.)
- **`triggers[]`** → a map of `eventName → sequence (+ socket)`. When an authored
  animation notify event of that name fires on the entity, the engine spawns +
  attaches the combo automatically. **No script required.**

This is the recommended default for character-driven combos.

### B. Script, reacting to an animation event — `IAnimationEventListener`
Implement the interface to receive animation events, then spawn/cue a combo:

```mt
@Script
public class Caster implements IAnimationEventListener {
    private int comboId = 0;

    @Override
    public function onAnimationEvent(string eventName, string stateName, string payload): void {
        int self = Entity::self();
        if (eventName == "cast_start") {
            comboId = VFX::spawnComboLooping("assets/vfx/library/example_cast_combo.vfVFXSequence", 0.0, 0.0, 0.0);
            VFX::attachComboToSocket(comboId, self, "hand_R");
        } else if (eventName == "cast_release") {
            VFX::triggerComboCue(comboId, "release");
        }
    }
}
```

`implements IAnimationEventListener` is **required** — without it the engine never
calls `onAnimationEvent` (it's silently skipped, not an error). Alternatively poll
without the interface: `string e = Animator::pollEvent(self);` (returns the next
event name, `""` when none).

### C. Script, anytime — direct `spawnCombo`
No animation or interface needed — call it from any logic (hit, click, timer):

```mt
public function onHit(float x, float y, float z): void {
    VFX::spawnCombo("assets/vfx/library/example_impact_combo.vfVFXSequence", x, y, z);
    // auto-destroys when its steps finish
}
```

---

## 4. Sockets

A **socket** is a named attach point defined on a **skeletal mesh** (`.vfMesh`,
authored in the Animation Preview socket panel) that rides a bone. At runtime a combo
resolves it by **exact name** against the entity it's attached to.

- The name must **exactly match** a socket on the **target entity's mesh**
  (`hand_R`, `Muzzle`, …). Empty = no socket (combo sits at the entity transform).
  A name that doesn't exist **warns and falls back** — it never crashes.
- **Step** socket = that one step follows its own socket (per-step override).
  **Component / trigger** socket = the whole combo attaches there.
- In the **sequence editor**, drop a reference `.vfMesh` in the toolbar to turn the
  per-step **Socket** field into a **dropdown** of that mesh's real sockets (the
  reference mesh is editor-only and not saved; the chosen names are saved).
- The editor **preview does not show socket-follow** (no skeleton in the preview) —
  attach to a real animated entity in Play mode to see it.

---

## 5. Script API (`VFX.mt`)

| Native | Returns | Does |
|---|---|---|
| `VFX::spawnCombo(path, x, y, z)` | `int comboId` | spawn + play; auto-destroys when steps finish |
| `VFX::spawnComboLooping(path, x, y, z)` | `int comboId` | spawn + play; stays alive after steps finish (you own it) |
| `VFX::attachComboToSocket(comboId, entityId, socket)` | — | attach the combo to a socket; children follow it |
| `VFX::detachCombo(comboId)` | — | stop following the socket |
| `VFX::setComboPosition(comboId, x, y, z)` | — | move it |
| `VFX::triggerComboCue(comboId, cueName)` | — | fire the matching cue-driven steps |
| `VFX::stopCombo(comboId)` | — | stop (loop children destroyed, others finish naturally) |
| `VFX::resetCombo(comboId)` | — | rewind to the start |
| `VFX::destroyCombo(comboId)` | — | destroy the combo and all children |
| `VFX::comboIsPlaying(comboId)` | `bool` | is it still running |

**`path`** is the project-relative path to the **`.vfVFXSequence`** file (not a
`.vfVFX`), e.g. `"assets/vfx/library/example_impact_combo.vfVFXSequence"` — the same
convention as `VFX::spawnAt`. A bad/missing path returns `0` and logs a warning.

**`comboId`** identifies one running instance. Keep it if you'll attach/move/cue/stop
it later; ignore it for fire-and-forget. It's valid until the combo finishes (or you
`destroyCombo` it); after that it's stale and every combo call on it is a safe no-op
(`comboIsPlaying` returns `false`). A looping combo (or one with looping steps) is
yours to `destroyCombo`.

---

## 6. How animation events fire (the trigger side)

Two timelines are involved — don't conflate them:

```
ANIMATION CLIP:            ...———[ combo_fire @ normalizedTime 0.3 ]———...   (fires once → starts the combo)
                                          │
                                          ▼
VFX SEQUENCE (starts here): step0 @0.0 ─ step1 @0.05 ─ step2 @0.1 ─ step3 @0.3 ─ (cue steps wait for triggerComboCue)
```

- An **animation event** (authored on a `.vfAnim` clip timeline *or* an animator
  state) fires when the playhead **crosses** its `normalizedTime` (0–1) — on the first
  frame after crossing, so precision = frame rate.
- It fires **once per pass**: once for a non-looping/one-shot state, **once per loop**
  for a looping clip. There is no built-in "fire once ever" flag — put combo triggers
  on non-looping states, or guard with a bool in script.
- Both `.vfAnim` clip events and animator-state events fire (this was a runtime fix in
  VK-1425 — clip events used to be silently dropped).
- The combo's **steps** then fire at their **Start Times** measured from the moment the
  combo started — *not* all at the start. Cue steps wait for `triggerComboCue`.

Note: there is a **1-frame latency** between an animation event and the data-driven
combo spawn (the trigger is queued and consumed on the next VFX tick) — visually
imperceptible.

---

## 6b. Step-output events — spawn a step at a particle event location (VK-1524)

A step can start at the **exact world location** where an earlier step's particle
produced a **death** or **collision**, instead of at the combo/socket transform. This
connects effects such as **projectile → impact**, **trail → explosion**, and
**charge → release** without gameplay code or duplicated positions.

Two roles:

- **Source step** — a normal Time/Cue **VFX** step that *publishes a named spatial output*
  when its particles emit an event. In the step inspector set **Step Output ▸ Output Name**
  (e.g. `impact`) and **Output Event** (On Death / On Collision). The source's `.vfVFX` must
  have that event **enabled** with its **Notify (CPU)** flag on (in the VFX editor's event
  section) so the engine publishes the event's location. `Notify` lets a source publish
  *without* also spawning a sub-emitter, so you don't get a stray effect at the impact point.
- **Receiver step** — set its **Trigger** to **Step Output**, pick the **Source Step** and
  **Event Name**, a **Consumption** mode, and which impact fields to **inherit**. When the
  bound event fires, the receiver spawns at the event's world position.

**Consumption modes**

| Mode | Behavior |
|---|---|
| **First Event** | spawn the receiver **once**, from the first matching event. |
| **Every Event** | spawn **once per matching event**, bounded by the receiver's **Event Budget** (max simultaneous live event-children) and a per-frame safety cap. |

**Transform** — the event position is **world-space**; the receiver's authored **Local
Position/Euler/Scale** are applied *after* as an offset (`world = translate(eventPos) ·
localTRS`). The combo/socket parent is **not** applied again (no double-transform), and a
receiver's **socket is ignored** (it is world-anchored).

**Inheritance** — optionally take the impact's **velocity** (→ emit direction + start speed),
**color** (→ start color), and **size** (→ start size). (Normal is reserved — no producer yet.)

**Determinism / preview** — particle events are runtime and non-deterministic, so they run
**only during forward Play**, never in the sequence-editor preview or during seek/scrub. A
StepOutput clip shows a **`[waiting for event]`** label in the timeline and never spawns in
the CPU preview — attach to a real Play-mode combo to see it fire. Old (`≤ 1.5`) assets have
no bindings and keep their exact Time/Cue behavior.

**Validation** — the editor flags a missing/invalid source, a self-dependency, a dependency
cycle, an empty event name, a source that isn't a VFX step or doesn't publish the bound name,
and a step that is both a source and a receiver.

### Integration contract for VK-1501 (GPU event → child)

The receiving path is deliberately producer-agnostic. An event is consumed as a
`vfx::VFXEventPayload` (an alias of `vfx::VFXCuePayload` in `utilities/vfx`, extended with
`velocity`/`normal`) carrying a **world position** plus optional velocity/color/scalar/custom;
the runtime service reverse-maps a `VFXParticleEventNotification`'s `parentInstanceId` to the
owning combo step and spawns the receiver at `translate(position) · localTRS`. VK-1501's GPU
fast path stays GPU-internal, but when it **falls back to CPU** it publishes the *same*
`VFXParticleEventNotification` — so it feeds this identical receiving seam with **no further
sequence-schema change**. A future GPU-originated *named* spatial output only needs to populate
that payload; the sequence binding, consumption modes, and world-anchoring are already in place.

## 7. Serialization

- The **`.vfVFXSequence` asset** (steps, transforms, sockets, overrides) is its own
  JSON file; saving also writes a `.vfmeta` listing each child `.vfVFX` as a dependency.
- A **`VFXSequenceComponent`** on an entity round-trips through both **scene** and
  **prefab** save/load (key `"vfxSequence"`); `runtimeComboId` is transient and not saved.

---

## 8. Example combos (`assets/vfx/library/`)

| Example | Shows |
|---|---|
| `example_impact_combo.vfVFXSequence` | 4 one-shot steps at staggered times (muzzle → sparks → dust → smoke); plays standalone. |
| `example_cast_combo.vfVFXSequence` | a **looping** `fire_loop` charge on the `hand_R` socket + a **cue-driven** `magic_heal` release (`cueName: "release"`). |

```mt
// Standalone impact at a world position
int impact = VFX::spawnCombo("assets/vfx/library/example_impact_combo.vfVFXSequence", x, y, z);

// Cast: looping charge follows the hand; fire the release cue on attack
int cast = VFX::spawnComboLooping("assets/vfx/library/example_cast_combo.vfVFXSequence", x, y, z);
VFX::attachComboToSocket(cast, casterEntity, "hand_R");
VFX::triggerComboCue(cast, "release");
VFX::destroyCombo(cast);
```
