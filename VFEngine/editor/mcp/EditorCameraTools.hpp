#pragma once

namespace mcp
{
    class ToolRegistry;
}

namespace editor
{
    class EditorCamera;

    // Editor-side MCP tools (VK-1651): camera_get, camera_set, camera_focus. They live
    // in Editor.exe because the editor camera is editor state, not a service. All run
    // on the main thread; `camera` is owned by the ViewPort window, which outlives
    // the MCP server (EditorHandler::cleanUp stops the server before UI teardown).
    void registerEditorCameraTools(mcp::ToolRegistry& registry, EditorCamera* camera);
}
