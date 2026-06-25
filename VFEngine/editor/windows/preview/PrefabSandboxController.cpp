#include "print/Log.hpp"
#include "PrefabSandboxController.hpp"
#include "PrefabPreviewWindow.hpp"
#include "PrefabRigLiveDescBuilder.hpp" // buildPrefabRigDescFromEntity / LiveRigBuildResult
#include "PrefabRigValidation.hpp"      // prefabrigval::validatePartRefs / countPartsWithMissingRefs
#include "events/EventDispatcher.hpp"
#include "events/render/PrefabRigPreviewEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp" // LoadPrefabCommand
#include "events/scene/EntityTransformEvents.hpp"   // Create/Delete/Get + MarkPreviewSandbox + TransformChanged
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <functional>

using json = nlohmann::json;

namespace windows
{
    // ----------------------------------------------------------------------
    // VK-1433 Phase 4 — sandbox lifecycle (replaces the JSON-parse spine)
    // ----------------------------------------------------------------------
    namespace
    {
        // Count the entities in a visible sandbox subtree for the info panel, using the same
        // active-state rules as the live builder. The sandbox root's inactive flag is preview
        // isolation only, so it is always counted and its children are evaluated normally.
        uint32_t countSandboxEntities(services::EntityHandle entity,
                                      services::EntityHandle sandboxRoot)
        {
            if (!entity.isValid()) return 0;
            events::scene::GetEntityQuery q;
            q.entity = entity;
            auto data = events::EventDispatcher::instance().query(q);
            if (!data.has_value()) return 0;
            if (entity != sandboxRoot && !data->isActive) return 0;
            uint32_t n = 1;
            for (const services::EntityHandle& child : data->children)
                n += countSandboxEntities(child, sandboxRoot);
            return n;
        }
    }

    void PrefabSandboxController::readPrefabHeader()
    {
        // Only the prefab's display name / version come from the JSON header now; the entity
        // hierarchy is the LoadPrefab'd sandbox subtree. A missing header is non-fatal.
        ctx.prefabName = "Unnamed";
        ctx.prefabVersion = "unknown";
        try
        {
            std::ifstream file(w.prefabPath);
            if (!file.is_open()) return;
            json prefabJson = json::parse(file);
            ctx.prefabVersion = prefabJson.value("version", "unknown");
            if (auto it = prefabJson.find("prefab"); it != prefabJson.end() && it->is_object())
                ctx.prefabName = it->value("name", "Unnamed");
        }
        catch (const std::exception& e)
        {
            vfLogWarning("Prefab preview header read failed for '{}': {}", w.prefabPath, e.what());
        }
    }

    void PrefabSandboxController::openSandbox()
    {
        readPrefabHeader();
        initPreviewRenderer();

        // LoadPrefab into an isolated sandbox subtree (parent = scene root), then tag it so it is
        // excluded from the main passes + the scene serializer (mirrors UILayerBuilderWindow).
        events::scene::LoadPrefabCommand loadCmd;
        loadCmd.filePath = w.prefabPath;
        loadCmd.parent = std::nullopt;
        auto loaded = events::EventDispatcher::instance().execute(loadCmd);
        if (!loaded.has_value() || !loaded->isValid())
        {
            ctx.errorMessage = "Failed to load prefab";
            ctx.loadFailed = true;
            return;
        }
        ctx.sandboxRoot = *loaded;

        events::scene::MarkPreviewSandboxCommand markCmd;
        markCmd.entity = ctx.sandboxRoot;
        markCmd.tagged = true;
        events::EventDispatcher::instance().execute(markCmd);

        // Subscribe to transform edits so inspector/numeric/gizmo/undo edits drive a CHEAP transform
        // sync (see the header). Setting a bool in the callback is safe even though it fires mid-draw
        // during a SetTransformCommand: publish() snapshots subscribers and invokes them with its lock
        // released. Engine is alive here; unsubscribe lives in closeSandbox(), never the dtor.
        if (!transformChangedToken.isValid())
        {
            transformChangedToken =
                events::EventDispatcher::instance().subscribe<events::scene::TransformChangedNotification>(
                    [this](const events::scene::TransformChangedNotification&)
                    {
                        if (ctx.sandboxRoot.isValid())
                            transformsDirty = true;
                    });
        }

        ctx.prefabLoaded = true;
        rebuildRigFromSandbox();

        // Initial selection: the first mesh part if there is one (so the authoring panels are
        // immediately usable), otherwise the sandbox root. selectEntity drives the window-local
        // selection that highlights the hierarchy row and feeds the embedded inspector; selectPart
        // drives the part-indexed authoring panels. Both must be set so the tree, inspector, and
        // panels agree (the hierarchy-click path sets them together too).
        if (!ctx.partEntities.empty())
        {
            w.hierarchyPanel.selectEntity(ctx.partEntities[0]);
            w.authoringController.selectPart(0);
        }
        else
        {
            w.hierarchyPanel.selectEntity(ctx.sandboxRoot);
        }
    }

    void PrefabSandboxController::closeSandbox()
    {
        cleanUpPreviewRenderer();

        // Unsubscribe here (engine alive) — NOT in the dtor, which can run after the EventDispatcher is
        // gone at shutdown. Idempotent: guarded by isValid(), reset to the default (invalid) token.
        if (transformChangedToken.isValid())
        {
            events::EventDispatcher::instance().unsubscribe(transformChangedToken);
            transformChangedToken = {};
        }
        transformsDirty = false;

        if (ctx.sandboxRoot.isValid())
        {
            // Destroy the whole tagged sandbox subtree. No untag needed — it's gone.
            events::scene::DeleteEntityCommand del;
            del.entity = ctx.sandboxRoot;
            events::EventDispatcher::instance().execute(del);
            ctx.sandboxRoot = services::EntityHandle::invalid();
        }
        ctx.partEntities.clear();
        ctx.selectedSandboxEntity = services::EntityHandle::invalid();
        w.hierarchyPanel.onSandboxClosed();
        lastStructureSignature.clear();
        structureCheckCountdown = 0; // re-baseline the signature on the next open's first frame
    }

    void PrefabSandboxController::rebuildRigFromSandbox()
    {
        // Re-derive the rig description (and the parallel source entities) from the live sandbox
        // subtree, then rebuild the preview. Called on open and after every structural edit.
        LiveRigBuildResult built = buildPrefabRigDescFromEntity(ctx.sandboxRoot);
        ctx.rigDesc = std::move(built.desc);
        ctx.partEntities = std::move(built.partEntities);
        ctx.sandboxEntityCount = countSandboxEntities(ctx.sandboxRoot, ctx.sandboxRoot);

        revalidateRefs();
        buildPreviewFromDesc();

        // The assembly was rebuilt from the fresh desc.ik, so the IK panel must re-pull its chain
        // copies on next draw (it caches them once via chainsLoaded).
        ctx.chainsLoaded = false;

        // VK-1433 Phase 4b — a structural edit (reparent/reorder/create/delete/hide) can shuffle the
        // DFS-pre-order part indices, so re-map the selected part FROM the live selection (which is an
        // entity, stable across the rebuild). If the selection is no longer a part (or was deleted),
        // fall back to re-clamping into range. selectPart() re-pulls the part's sockets.
        const int mapped = w.hierarchyPanel.partForEntity(w.hierarchyPanel.selectedEntity());
        if (mapped >= 0)
        {
            ctx.selectedPart = mapped;
            w.authoringController.pullEditSocketsForPart(ctx.selectedPart);
        }
        else if (ctx.selectedPart >= static_cast<int>(ctx.rigDesc.parts.size()))
        {
            ctx.selectedPart = ctx.rigDesc.parts.empty() ? -1 : static_cast<int>(ctx.rigDesc.parts.size()) - 1;
        }
    }

    void PrefabSandboxController::syncTransformsFromSandbox()
    {
        // CHEAP path for a transform VALUE edit (gizmo / inspector / numeric / undo replay). Re-derive
        // the rig DTO from the live sandbox — CQRS only, resolves mesh refs to PATHS, never loads
        // geometry — then push a transform-ONLY update to the controller (no waitIdle / pipeline
        // teardown / mesh reload). This is the prefab-view analogue of UILayerBuilderWindow's
        // rebuild-on-edit, which is cheap there only because UI elements don't reload from disk.
        if (!ctx.previewInitialized || !ctx.sandboxRoot.isValid())
            return;

        LiveRigBuildResult built = buildPrefabRigDescFromEntity(ctx.sandboxRoot);
        ctx.rigDesc = std::move(built.desc);
        ctx.partEntities = std::move(built.partEntities);

        services::events::prefabrigpreview::UpdatePrefabRigPreviewTransformsCommand cmd;
        cmd.instanceId = ctx.instanceId;
        cmd.desc = ctx.rigDesc;
        const bool applied = events::EventDispatcher::instance().execute(cmd);

        // Structure drifted out from under the cheap path (part count / mesh / parent / attach socket
        // changed) — fall back to the full rebuild, which reloads + re-maps selection. NOT expected on
        // a pure transform edit; this is the safety net.
        if (!applied)
            rebuildRigFromSandbox();
    }

    // ----------------------------------------------------------------------
    // Rig preview lifecycle (service boundary)
    // ----------------------------------------------------------------------
    void PrefabSandboxController::initPreviewRenderer()
    {
        if (ctx.previewInitialized) return;

        services::events::prefabrigpreview::InitPrefabRigPreviewCommand cmd;
        cmd.instanceId = ctx.instanceId;
        events::EventDispatcher::instance().execute(cmd);
        ctx.previewInitialized = true;
    }

    void PrefabSandboxController::buildPreviewFromDesc()
    {
        if (!ctx.previewInitialized) return;

        // Always (re)dispatch the build, even when rigDesc has zero parts (hide-all). An empty desc
        // is a VALID empty preview: the controller's buildFromDesc destroys its pipelines, clears the
        // assembly and reports built=true, so render() clears the offscreen image to the background.
        // Bailing on an empty desc here would leave the previous (still-animating) build on screen.
        services::events::prefabrigpreview::BuildPrefabRigPreviewCommand cmd;
        cmd.instanceId = ctx.instanceId;
        cmd.desc = ctx.rigDesc;
        ctx.previewBuilt = events::EventDispatcher::instance().execute(cmd);

        if (!ctx.previewBuilt)
        {
            vfLogWarning("Prefab rig preview build produced no parts for: {}", w.prefabPath);
        }
    }

    void PrefabSandboxController::cleanUpPreviewRenderer()
    {
        if (previewCleanedUp) return;

        if (ctx.previewInitialized)
        {
            services::events::prefabrigpreview::CleanUpPrefabRigPreviewCommand cmd;
            cmd.instanceId = ctx.instanceId;
            events::EventDispatcher::instance().execute(cmd);
        }
        previewCleanedUp = true;
    }

    // ----------------------------------------------------------------------
    // Phase 2: broken-ref validation
    // ----------------------------------------------------------------------
    void PrefabSandboxController::revalidateRefs()
    {
        // Inject the real filesystem predicate; the free helper is unit-tested with a fake.
        ctx.partRefStatuses = prefabrigval::validatePartRefs(ctx.rigDesc, [](const std::string& p)
        {
            std::error_code ec;
            return std::filesystem::exists(p, ec);
        });
        ctx.missingRefCount = prefabrigval::countPartsWithMissingRefs(ctx.partRefStatuses);
    }

    std::vector<uint64_t> PrefabSandboxController::sandboxStructureSignature() const
    {
        // DFS over the visible sandbox subtree, emitting each entity id followed by its component
        // type-id list (a separator sentinel between entities). A change (component add/remove via the
        // embedded inspector, a create/delete) shifts this vector, which the caller uses to re-derive
        // the rig without coupling to any specific Add/Remove command.
        std::vector<uint64_t> sig;
        if (!ctx.sandboxRoot.isValid()) return sig;

        std::function<void(services::EntityHandle)> walk = [&](services::EntityHandle e)
        {
            if (!e.isValid()) return;
            events::scene::GetEntityQuery q;
            q.entity = e;
            auto data = events::EventDispatcher::instance().query(q);
            if (!data.has_value()) return;
            sig.push_back(e.id);
            for (services::ComponentTypeId c : data->components)
                sig.push_back(static_cast<uint64_t>(c));
            sig.push_back(~0ull); // entity separator sentinel
            for (const services::EntityHandle& child : data->children)
                walk(child);
        };
        walk(ctx.sandboxRoot);
        return sig;
    }

    void PrefabSandboxController::tickEndOfFrame()
    {
        // VK-1433 Phase 4b — detect a structural component change made through the embedded inspector
        // (Add/Remove) — or any create/delete that didn't already rebuild — by diffing the sandbox
        // structure signature against last frame's. A change re-derives the rig. (Mutations issued by
        // the tree itself already call rebuildRigFromSandbox(), so this only fires for the inspector's
        // add/remove, which dispatch their own commands.)

        // Fix B4 — the signature walk issues per-node CQRS over the whole subtree, so throttle it
        // to roughly every kStructureCheckInterval frames instead of every frame. countdown == 0
        // means "check this frame"; it is reset to the interval after each walk. The countdown is
        // 0 at the first frame after an open (member default 0; closeSandbox resets it), so the
        // baseline signature is captured immediately rather than after a throttle delay.
        bool rebuiltThisFrame = false;
        if (structureCheckCountdown > 0)
        {
            --structureCheckCountdown;
        }
        else
        {
            structureCheckCountdown = kStructureCheckInterval;
            std::vector<uint64_t> sig = sandboxStructureSignature();
            if (sig != lastStructureSignature)
            {
                const bool firstFrame = lastStructureSignature.empty();
                lastStructureSignature = std::move(sig);
                if (!firstFrame)
                {
                    ctx.dirty = true;
                    rebuildRigFromSandbox();
                    rebuiltThisFrame = true;
                }
            }
        }

        // VK-1433 — transform VALUE edits don't shift the structure signature; the
        // TransformChangedNotification subscription flags them here. Apply the CHEAP transform-only
        // sync (no mesh reload) — but skip it if a full structural rebuild already ran this frame
        // (that path captured the new transforms too). Consume the flag unconditionally so a
        // structural rebuild never leaves it set for a redundant next-frame sync.
        if (transformsDirty)
        {
            transformsDirty = false;
            if (!rebuiltThisFrame)
                syncTransformsFromSandbox();
        }
    }
}
