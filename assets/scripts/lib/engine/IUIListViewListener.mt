// IUIListViewListener - Interface for receiving UI list view events
// Implement this interface in @Script classes to receive list callbacks
// List events are broadcast to ALL scripts implementing this interface
//
// Usage:
//   import engine::IUIListViewListener;
//
//   @Script
//   public class UnitRosterController implements IUIListViewListener {
//       @Override
//       public function onListSelectionChanged(int listEntityId, string entityName,
//                                              int previousIndex, int newIndex): void {
//           Log::info("Selected row " + newIndex + " in " + entityName);
//       }
//   }

interface IUIListViewListener {
    // Called when the selected item changes (click or UI::setListSelectedIndex)
    function onListSelectionChanged(int listEntityId, string entityName,
                                    int previousIndex, int newIndex): void;
}
