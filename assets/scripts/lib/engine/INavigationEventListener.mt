// INavigationEventListener - Interface for receiving navigation callbacks
// Implement this interface in your script class to receive notifications
// when a NavmeshAgent reaches its destination or gets stuck.
//
// Usage:
//   @Script
//   public class MyNPC implements INavigationEventListener {
//       public function onDestinationReached(): void {
//           Log::info("Arrived at destination!");
//       }
//       public function onPathBlocked(): void {
//           Log::warn("Path is blocked!");
//       }
//   }

interface INavigationEventListener {
    function onDestinationReached(): void;
    function onPathBlocked(): void;
}
