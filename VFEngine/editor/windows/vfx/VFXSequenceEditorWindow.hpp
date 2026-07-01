#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "../preview/PreviewWindowChrome.hpp"
#include <vfx/VFXSequenceTypes.hpp>
#include <providers/vfx/IVFXPreviewProvider.hpp>
#include <math/Frustum.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vfx
{
    struct VFXData;
}

namespace editor::vfxeditor
{
    class VFXPreviewPanel;
}

namespace windows
{
    // Authoring window for a .vfVFXSequence "combo" asset: an ordered list of
    // child .vfVFX placements in time (or behind a named cue), each with an
    // optional local transform, socket, and name-keyed parameter overrides.
    // Mirrors VFXEditorWindow's lifecycle (path ctor, dirty '*' title, save
    // publishes AssetSavedNotification; shared asset services own dependency metadata).
    class VFXSequenceEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string seqPath;
        std::string windowTitle;
        std::unique_ptr<vfx::VFXSequenceData> data;
        int selectedStep = -1;
        bool isOpen = true;
        bool isDirty = false;
        bool needsInit = true;

        // Timeline scrub. Drives the real GPU composited preview (VK-1451): every
        // step is rendered at once into one offscreen image, deterministically seeded,
        // with play/pause/seek/rate/prewarm transport mirroring AnimationTimelinePanel.
        float previewTime = 0.0f;
        bool previewPlaying = true;             // auto-play the composited combo on open
        bool previewLoop = true;
        bool previewDirty = true;               // rebuild + re-send the sequence desc
        uint32_t previewSeed = 0;               // 0 => auto (asset seed, then random)
        float previewRate = 1.0f;
        float previewPrewarm = 0.0f;

        // Per-path .vfVFX cache so rebuilding the composite on a timing/seed/marker edit
        // doesn't re-read every step's file from disk each frame (Reload clears it).
        mutable std::unordered_map<std::string, std::shared_ptr<vfx::VFXData>> vfxCache;

        std::unique_ptr<editor::vfxeditor::VFXPreviewPanel> previewPanel;

        // Bottom timeline built on ImSequencer: one draggable clip per step
        // (drag = edit Start Time, drag the right edge = edit Duration).
        class SequenceTimeline;                 // ImSequencer adapter (defined in .cpp)
        std::unique_ptr<SequenceTimeline> timeline;
        int previewFrame = 0;
        int timelineFirstFrame = 0;
        bool timelineExpanded = true;

        // Optional reference mesh (editor-only, not saved): when set, the per-step
        // Socket field becomes a dropdown of that mesh's authored sockets.
        std::string socketMeshPath;
        std::vector<std::string> socketNames;

        editor::preview::WindowMaximizer maximizer;
        ImVec2 initialSize{0.0f, 0.0f};
        bool sizeSaved = false;

        bool showBounds = false; // VK-1453 — toggle the aggregate-bounds overlay

    public:
        explicit VFXSequenceEditorWindow(const std::string& seqPath);
        ~VFXSequenceEditorWindow() override;    // out-of-line (VFXPreviewPanel is incomplete here)

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

        const std::string& getSequencePath() const { return seqPath; }

    private:
        void loadSequence();
        void saveSequence();

        void drawToolbar();
        void drawValidationStrip();
        void drawStepList();
        void drawStepInspector();
        void drawTimeline();
        void drawMarkersRow();          // one-shot event-marker editor under the timeline
        void drawOverrideList(std::vector<vfx::VFXParamOverride>& overrides, const char* label);
        void drawCuePayload(vfx::VFXCuePayload& payload);

        // Real GPU composited preview (all steps at once).
        void drawPreviewViewport();
        // Build the full composited descriptor from the current sequence data
        // (loads each step's .vfVFX, applies overrides, attaches timing + derived seed).
        services::VFXSequencePreviewDesc buildSequenceDesc() const;

        // VK-1453 — aggregate bounds over the steps (union of each child's resolved
        // bounds transformed by its local placement); recalc captures it as Fixed.
        math::AABB computeSequenceBoundsUnion() const;
        void recalcSequenceBounds();

        void loadSocketNames();             // read sockets from socketMeshPath
        void drawSocketField(vfx::VFXSequenceStep& step); // dropdown if a mesh is set, else text
    };
}
