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
        bool isDraggingPreview = false;
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

        void play();
        void pause();
        void stop();
        bool getIsPlaying() const { return isPlaying; }

    private:
        void handleInput();
        void drawPlaybackControls();
    };
}
