# Starter Behavior Trees

Brain trees for AI units, exercising the reactive-condition / SubTree features.
Scripts they reference live in `assets/scripts/game/ai/` (run **Build Scripts** after changes).

| Tree | Behavior |
|------|----------|
| `Guard.vfBehaviorTree` | Order slot (move order preempts everything) → engage visible enemies → idle |
| `Patrol.vfBehaviorTree` | Walk patrolA↔patrolB, drop into Engage when an enemy is spotted |
| `HarvesterLoop.vfBehaviorTree` | Gather/deposit loop, flees from enemies instead of fighting |
| `Engage.vfBehaviorTree` | Shared chase+attack subtree (placeholder damage), referenced by Guard/Patrol |

## Setup

1. Add a **BehaviorTreeComponent** to the unit and point it at one of these `.vfBehaviorTree` files.
2. The unit needs a **NavmeshAgent** component and a baked navmesh.
3. Perception finds entities by name: name hostile entities `Enemy` (or change the
   `enemyName` blackboard default in the tree).
4. Patrol/Harvester routes: edit the `patrolA`/`patrolB` / `resourcePos`/`homePos`
   blackboard defaults, or write them at runtime via `Blackboard.mt` natives.

## Order-slot pattern (Guard.vfBehaviorTree)

Player command code issues an order by writing two blackboard keys:

```
Blackboard::setVec3(unitId, "orderPos", x, y, z);
Blackboard::setBool(unitId, "hasOrder", true);
```

The top-priority `Has Order?` condition (abort mode `Both`) instantly preempts
whatever the unit is doing; on arrival the tree clears `hasOrder` and the unit
returns to autonomous behavior. Setting `hasOrder` back to `false` mid-move
cancels the order (self abort stops the nav agent).

## Debugging

Open the tree in the Behavior Tree editor, enter play mode, and pick the unit
under the **Debug** menu: node borders show live status (yellow=Running,
green=Success, red=Failure) and the blackboard panel becomes live + editable —
flip `enemyVisible` by hand to test interrupts. Saving with Ctrl+S hot-reloads
the tree (and any tree embedding it via SubTree) without leaving play mode.
