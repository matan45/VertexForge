// IUITabsListener - Interface for receiving UI tabs events
// Implement this interface in @Script classes to receive tab switch callbacks
// Unlike collision callbacks, tabs events are broadcast to ALL scripts implementing this interface
//
// Usage:
//   import engine::IUITabsListener;
//
//   @Script
//   public class TabController implements IUITabsListener {
//       @Override
//       public function onTabSelected(int entityId, string entityName, int tabIndex): void {
//           Log::info("Tab selected: " + tabIndex);
//       }
//
//       @Override
//       public function onTabChanged(int entityId, string entityName, int newTabIndex, int previousTabIndex): void {
//           Log::info("Tab changed from " + previousTabIndex + " to " + newTabIndex);
//       }
//   }

interface IUITabsListener {
    // Called every time a tab is clicked (even if already active)
    function onTabSelected(int entityId, string entityName, int tabIndex): void;

    // Called only when the active tab actually changes
    function onTabChanged(int entityId, string entityName, int newTabIndex, int previousTabIndex): void;
}
