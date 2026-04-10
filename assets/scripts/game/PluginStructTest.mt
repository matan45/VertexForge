// PluginStructTest — VK-1291: Test struct-in-container access from mType
// Requires: PluginAPITest plugin loaded, entity with TestComponent attached

import * from "engine/Log.mt";
import * from "engine/Entity.mt";
import * from "engine/PluginComponent.mt";
import * from "BuffEntry.mt";

@Script
public class PluginStructTest {
    private bool tested = false;

    public constructor() {
    }

    public function onStart(): void{

    }

    public function onUpdate(float deltaTime): void {
        if (tested) return;
        tested = true;

        int self = Entity::self();

        if (!PluginComponent::has(self, "TestComponent")) {
            Log::info("[PluginStructTest] Entity has no TestComponent — skipping");
            return;
        }

        Log::info("=== VK-1291 Plugin Struct Test ===");

        // --- Unified get() with primitive cast ---
        int hp = (int)PluginComponent::get(self, "TestComponent", "health");
        float spd = (float)PluginComponent::get(self, "TestComponent", "speed");
        bool active = (bool)PluginComponent::get(self, "TestComponent", "isActive");
        Log::info("health=" + hp + " speed=" + spd + " isActive=" + active);

        // --- Array of primitives (vector<int> scores) ---
        int arrSize = PluginComponent::getArraySize(self, "TestComponent", "scores");
        Log::info("scores size=" + arrSize);
        for (int i = 0; i < arrSize; i = i + 1) {
            int score = PluginComponent::getArrayInt(self, "TestComponent", "scores", i);
            Log::info("  scores[" + i + "] = " + score);
        }

        // --- Map of primitives (map<string,float> stats) ---
        string[] keys = PluginComponent::getMapKeys(self, "TestComponent", "stats");
        Log::info("stats keys count=" + keys.length);
        for (int i = 0; i < keys.length; i = i + 1) {
            float val = PluginComponent::getMapFloat(self, "TestComponent", "stats", keys[i]);
            Log::info("  stats[" + keys[i] + "] = " + val);
        }

        // --- Array of structs (vector<BuffEntry> buffs) ---
        int buffCount = PluginComponent::getArraySize(self, "TestComponent", "buffs");
        Log::info("buffs size=" + buffCount);
        for (int i = 0; i < buffCount; i = i + 1) {
            Object elem = PluginComponent::getArrayElement(self, "TestComponent", "buffs", i);
            if (elem isClassOf BuffEntry) {
                BuffEntry buff = (BuffEntry)elem;
                Log::info("  buffs[" + i + "] name=" + buff.name
                    + " duration=" + buff.duration
                    + " stacks=" + buff.stacks
                    + " isPermanent=" + buff.isPermanent);
            } else {
                Log::info("  buffs[" + i + "] = not a BuffEntry");
            }
        }

        // --- Map of structs (map<string,BuffEntry> namedBuffs) ---
        string[] namedKeys = PluginComponent::getMapKeys(self, "TestComponent", "namedBuffs");
        Log::info("namedBuffs keys count=" + namedKeys.length);
        for (int i = 0; i < namedKeys.length; i = i + 1) {
            Object mapElem = PluginComponent::getMapValue(self, "TestComponent", "namedBuffs", namedKeys[i]);
            if (mapElem isClassOf BuffEntry) {
                BuffEntry nb = (BuffEntry)mapElem;
                Log::info("  namedBuffs[" + namedKeys[i] + "] name=" + nb.name
                    + " duration=" + nb.duration
                    + " stacks=" + nb.stacks);
            } else {
                Log::info("  namedBuffs[" + namedKeys[i] + "] = not a BuffEntry");
            }
        }

        Log::info("=== VK-1291 Test Complete ===");
    }

     public function onDestroy(): void{

    }
}
