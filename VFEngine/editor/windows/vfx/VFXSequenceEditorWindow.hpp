#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <vfx/VFXSequenceTypes.hpp>
#include <memory>
#include <string>

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
    // publishes AssetSavedNotification + writes a dependency .vfmeta sidecar).
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

        // Timeline scrub. Drives the real GPU single-emitter preview
        // (VFXPreviewPanel / IVFXPreviewProvider) one step at a time: whichever
        // step is active at the playhead is loaded and played. The runtime plays
        // the full composited combo on the GPU in Play mode.
        float previewTime = 0.0f;
        bool previewPlaying = false;
        bool previewLoop = true;
        int previewActiveStep = -1;             // step currently loaded into the panel

        std::unique_ptr<editor::vfxeditor::VFXPreviewPanel> previewPanel;

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
        void drawStepList();
        void drawStepInspector();
        void drawTimeline();

        // Real GPU preview (one active step at a time).
        void drawPreviewViewport();
        int pickActiveStep() const;
        void syncPreviewToStep(int stepIndex);
    };
}
