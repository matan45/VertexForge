#pragma once

#include <providers/vfx/IVFXPreviewProvider.hpp>
#include <memory>
#include <functional>

namespace editor
{
    class OrbitCamera;
}

namespace editor::vfxeditor
{
    class VFXPreviewPanel
    {
    private:
        void* instanceId;
        std::unique_ptr<OrbitCamera> camera;
        bool needsInit = true;
        bool isDraggingOrbit = false;
        bool isDraggingPan = false;
        bool isPlaying = false;
        float lastFrameTime = 0.0f;

    public:
        using ParamsChangedCallback = std::function<void()>;

        explicit VFXPreviewPanel(void* instanceId);
        ~VFXPreviewPanel();

        void init();
        void draw();
        void cleanup();

        bool isInitialized() const { return !needsInit; }

        void setParams(const services::VFXPreviewParams& params);

        // VK-1451 — composited sequence preview. setSequence loads all steps into the
        // controller at once; seek/setRate are the deterministic transport controls.
        void setSequence(const services::VFXSequencePreviewDesc& desc);
        void seek(float seconds);
        void setRate(float rate);

        // The sequence window drives transport from its timeline, so it hides the
        // panel's built-in Play/Stop/Restart row.
        void setBuiltInControls(bool show) { builtInControls = show; }

        void play();
        void pause();
        void stop();
        bool getIsPlaying() const { return isPlaying; }

    private:
        void handleInput();
        void drawPlaybackControls();

        bool builtInControls = true;
    };
}
