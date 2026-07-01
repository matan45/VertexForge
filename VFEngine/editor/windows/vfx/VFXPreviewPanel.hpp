#pragma once

#include <providers/vfx/IVFXPreviewProvider.hpp>
#include <math/Frustum.hpp>
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

        // VK-1453 — draw a local-space AABB wireframe over the preview image (bounds
        // visualization). The caller passes the resolved bounds each frame; the panel
        // projects the 8 corners with its preview camera. Off by default.
        void setBoundsOverlay(bool show, const math::AABB& localBounds)
        {
            showBounds = show;
            overlayBounds = localBounds;
            hasOverlayBounds = true;
        }

        void play();
        void pause();
        void stop();
        bool getIsPlaying() const { return isPlaying; }

    private:
        void handleInput();
        void drawPlaybackControls();
        void drawBoundsOverlay(float imageMinX, float imageMinY, float imageSizeX, float imageSizeY);

        bool builtInControls = true;

        bool showBounds = false;
        bool hasOverlayBounds = false;
        math::AABB overlayBounds;
    };
}
