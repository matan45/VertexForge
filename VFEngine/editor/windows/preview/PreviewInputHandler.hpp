#pragma once

namespace editor
{
    class OrbitCamera;
}

namespace editor::preview
{
    class PreviewInputHandler
    {
    public:
        // Call each frame from within the preview viewport child window.
        // RMB = orbit, MMB = pan, Scroll = zoom
        static void handleInput(OrbitCamera* camera, bool& isDraggingOrbit, bool& isDraggingPan);
    };
}
