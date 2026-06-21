#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <vfx/VFXSequenceTypes.hpp>
#include <memory>
#include <string>

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

        // Timeline preview scrub (time-driven marker preview only; live child
        // spawning against the VFX preview provider is a follow-up — see .cpp).
        float previewTime = 0.0f;
        bool previewPlaying = false;

    public:
        explicit VFXSequenceEditorWindow(const std::string& seqPath);
        ~VFXSequenceEditorWindow() override = default;

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
    };
}
