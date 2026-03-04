#pragma once

namespace editor
{
    class EditorCamera;
}

namespace windows
{
    class EditorCameraWindow
    {
    private:
        bool visible = false;
        editor::EditorCamera* editorCameraRef = nullptr;

    public:
        void draw();

        void setEditorCamera(editor::EditorCamera* camera) { editorCameraRef = camera; }

        void show() { visible = true; }
        void hide() { visible = false; }
        bool isVisible() const { return visible; }
    };
}
