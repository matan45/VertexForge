// IAnimationEventListener - Interface for receiving animation events
// Implement this interface in @Script classes to receive callbacks when
// animation events fire during playback (e.g., footstep sounds, weapon attach points)
//
// Events are defined in the Animator asset editor on animation states.
// Each event has a name, normalized time (0-1), and optional payload string.
//
// Usage:
//   @Script
//   public class CharacterController implements IAnimationEventListener {
//       @Override
//       public function onAnimationEvent(string eventName, string stateName, string payload): void {
//           if (eventName == "Footstep") {
//               Audio::play3d(Entity::self(), "sounds/footstep.vfAudio");
//           } else if (eventName == "AttachWeapon") {
//               Socket::attach(weaponId, Entity::self(), payload);
//           }
//       }
//   }

interface IAnimationEventListener {
    // Called when an animation event fires during playback
    // eventName: the name of the event (e.g., "Footstep", "AttachWeapon")
    // stateName: the animation state that fired the event
    // payload: optional string data (e.g., socket name, sound path)
    function onAnimationEvent(string eventName, string stateName, string payload): void;
}
