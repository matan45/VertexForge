// IVFXComboCueListener - Interface for receiving VFX combo-sequence cue events (VK-1495)
// Implement this interface in @Script classes to receive a callback when a
// .vfVFXSequence timeline cue fires (authored event marker) or when a script
// calls VFX::triggerComboCue / VFX::triggerComboCuePayload.
//
// Fired ONLY by forward playback and manual triggers. Editor scrub / seek /
// prewarm / replay deliberately never fire onComboCue - so a listener will not
// see spurious cues while the timeline is being scrubbed.
//
//   comboId : the combo instance id (see VFX::spawnCombo)
//   cueName : the authored cue name on the marker / triggerComboCue call
//   payload : optional position / color / scalar (check the has* flags)
//
// Enables gameplay timing on the authored visual frame: camera shake, damage
// sync, SFX - all as a few lines of mType, no dedicated engine system.
//
// Usage:
//   import * from "ComboCuePayload.mt";
//
//   @Script
//   public class Impact implements IVFXComboCueListener {
//       @Override
//       public function onComboCue(int comboId, string cueName, ComboCuePayload payload): void {
//           if (cueName == "impact" && payload.hasScalar) {
//               Log::info("impact strength " + parsePrimitive(payload.scalar));
//           }
//       }
//   }

import * from "ComboCuePayload.mt";

interface IVFXComboCueListener {
    // Called when a combo-sequence cue fires during forward playback.
    function onComboCue(int comboId, string cueName, ComboCuePayload payload): void;
}
