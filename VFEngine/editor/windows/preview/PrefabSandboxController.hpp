#pragma once
#include "PrefabRigEditContext.hpp"
#include "events/EventTypes.hpp" // events::SubscriptionToken (transform-change subscription)
#include <cstdint>
#include <vector>

namespace windows { class PrefabPreviewWindow; }

namespace windows
{
    // VK-1443 — sandbox lifecycle + preview-renderer ownership sub-controller, split out of
    // PrefabPreviewWindow. Owns the LoadPrefab'd sandbox subtree lifecycle (open/close/rebuild), the
    // preview-renderer init/build/cleanup CQRS, broken-ref validation, the structure-signature throttle,
    // and the transform-change subscription. Shared edit state lives in the window (reached via `ctx`);
    // single-concern sandbox state lives here.
    class PrefabSandboxController
    {
    public:
        explicit PrefabSandboxController(PrefabRigEditContext& ctx, PrefabPreviewWindow& w)
            : ctx(ctx), w(w) {}

        // Sandbox lifecycle (the JSON-parse spine is retired). openSandbox() LoadPrefabs the prefab into
        // an isolated tagged subtree and re-derives the rig from it; closeSandbox() deletes that subtree
        // + tears down the renderer. rebuildRigFromSandbox() re-runs the live builder after every
        // structural edit and rebuilds the preview.
        void openSandbox();
        void closeSandbox();
        void rebuildRigFromSandbox();
        void readPrefabHeader(); // prefabName / prefabVersion from the .vfPrefab JSON header only

        // Rig preview lifecycle (service boundary; no Graphics/Core includes).
        void initPreviewRenderer();
        void buildPreviewFromDesc();
        void cleanUpPreviewRenderer();

        void revalidateRefs(); // fills partRefStatuses + missingRefCount from rigDesc on disk

        // CHEAP path for a transform VALUE edit: re-derive the rig DTO from the sandbox (CQRS-only —
        // resolves mesh refs to PATHS, never loads geometry) and push a transform-ONLY update to the
        // preview controller. Falls back to a full rebuild if the structure drifted.
        void syncTransformsFromSandbox();

        // Structure signature of the sandbox subtree (entity ids + their component type lists, DFS).
        std::vector<uint64_t> sandboxStructureSignature() const;

        // VK-1443 — the former end-of-draw block: structure-signature throttle (catches the embedded
        // inspector's Add/Remove) + the transform-dirty cheap sync. Called from draw() when the window
        // is open with a valid sandbox root.
        void tickEndOfFrame();

    private:
        PrefabRigEditContext& ctx;
        PrefabPreviewWindow& w;

        bool previewCleanedUp = false;

        // The signature walk issues per-node CQRS over the whole subtree, so running it every frame is
        // wasteful for an idle window. Structural edits are user-driven (component add/remove,
        // create/delete), so a few-frame detection latency is invisible — throttle the check to roughly
        // every kStructureCheckInterval frames. (Tree-driven mutations rebuild immediately; this only
        // catches the embedded inspector's Add/Remove, which can tolerate the latency.)
        static constexpr int kStructureCheckInterval = 12;
        int structureCheckCountdown = 0; // frames until the next signature check (0 == check this frame)
        std::vector<uint64_t> lastStructureSignature;

        // Transform VALUE edits (embedded inspector, numeric fields, the gizmo, undo replay) do NOT
        // shift the structure signature, so they are caught via a TransformChangedNotification
        // subscription instead: the callback sets transformsDirty, and tickEndOfFrame() applies a CHEAP
        // transform-only sync (syncTransformsFromSandbox — no mesh reload), unless a full structural
        // rebuild already ran that frame. Subscribed in openSandbox(), unsubscribed in closeSandbox() —
        // never the dtor (it can run after the EventDispatcher is gone at shutdown).
        bool transformsDirty = false;
        events::SubscriptionToken transformChangedToken;
    };
}
