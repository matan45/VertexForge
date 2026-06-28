#pragma once
#include "PrefabRigEditContext.hpp"
#include "../scene/EntityDetailsPanel.hpp" // VK-1433 Phase 4b embedded component inspector
#include "data/EntityHandle.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace windows { class PrefabPreviewWindow; }

namespace windows
{
    // VK-1443 — sandbox hierarchy tree + embedded entity inspector + window-local selection
    // sub-controller, split out of PrefabPreviewWindow. Owns the CQRS-recursive tree (add/remove/
    // rename/reparent/reorder/hide), the confirm-on-delete modal, the embedded component inspector,
    // and the window-local selection link to the part-indexed authoring panels.
    class PrefabHierarchyPanel
    {
    public:
        explicit PrefabHierarchyPanel(PrefabRigEditContext& ctx, PrefabPreviewWindow& w)
            : ctx(ctx), w(w) {}

        // VK-1433 Phase 4 — the hierarchy is a CQRS-recursive tree over the live sandbox subtree.
        void drawEntityTreePanel();
        // Embedded full component inspector for the selected sandbox entity (4b, O1).
        void drawEntityInspector();
        void drawDeleteConfirmPopup(); // "Delete '<name>' and its children?" modal

        // VK-1443 — clear inline-rename state when the sandbox closes (was inline in closeSandbox()).
        void onSandboxClosed();

        // Selection link between the live hierarchy and the part-indexed authoring panels. The
        // selection is WINDOW-LOCAL (no global SelectEntityCommand). selectedEntity() returns this
        // local handle; selectEntity() sets it.
        services::EntityHandle selectedEntity() const;
        void selectEntity(services::EntityHandle entity);
        // Part index whose source entity == `entity` (linear search over partEntities); -1 if none.
        int partForEntity(services::EntityHandle entity) const;

    private:
        PrefabRigEditContext& ctx;
        PrefabPreviewWindow& w;

        void drawEntityNode(services::EntityHandle entity, int depth);
        void drawReorderDropZone(services::EntityHandle parent, size_t index); // between-siblings insert
        // VK-1433 Phase 4d — shared create/delete used by the Hierarchy header toolbar, the per-node
        // context menu, and the Delete-key shortcut.
        services::EntityHandle createChildEntity(services::EntityHandle parent);
        void requestDeleteEntity(services::EntityHandle entity);
        bool deleteEntity(services::EntityHandle entity);
        // O2: would renaming the entity currently named `oldName` orphan a socket attachment that
        // resolves to it by name? Used to WARN (non-destructive) before a rename.
        bool renameWouldOrphanAttachment(const std::string& oldName) const;
        // Tooltip text listing a part's missing references (empty if none / out of range).
        std::string missingRefTooltip(int part) const;

        // VK-1433 Phase 4b — inline-rename state (clone of UILayerBuilderWindow). renamingEntity is the
        // node whose label is currently an InputText; renameFocusPending grabs keyboard focus once.
        services::EntityHandle renamingEntity = services::EntityHandle::invalid();
        bool renameFocusPending = false;
        char renameBuf[256] = {};

        // VK-1433 Phase 4b — embedded shared component inspector (O1).
        EntityDetailsPanel entityInspector;

        services::EntityHandle pendingDeleteEntity = services::EntityHandle::invalid();
        std::string pendingDeleteName;       // captured at request time for the modal message
        bool openDeleteConfirmPopup = false; // set by requestDeleteEntity, consumed in draw()
    };
}
