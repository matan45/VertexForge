#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "CullingStatsWindow.hpp"
#include "EditorCameraWindow.hpp"
#include "IBLWindow.hpp"
#include "ImportModalDialog.hpp"
#include "PhysicsConfigWindow.hpp"
#include "AudioConfigWindow.hpp"
#include "RenderConfigWindow.hpp"
#include "ProjectSettingsWindow.hpp"
#include "MainMenuBar.hpp"
#include "events/EventDispatcher.hpp"

namespace editor
{
    class EditorCamera;
}

namespace windows
{
    class MainImguiWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        int windowFlags;

        // Sub-windows
        CullingStatsWindow cullingStatsWindow;
        EditorCameraWindow editorCameraWindow;
        IBLWindow iblWindow;
        ImportModalDialog importDialog;
        PhysicsConfigWindow physicsConfigWindow;
        AudioConfigWindow audioConfigWindow;
        RenderConfigWindow renderConfigWindow;
        ProjectSettingsWindow projectSettingsWindow;
        MainMenuBar menuBar;

        // Event subscription
        events::SubscriptionToken sceneClearedToken;
        events::SubscriptionToken openImportDialogToken;

    public:
        explicit MainImguiWindow();
        ~MainImguiWindow() override;

        void draw() override;

        void setEditorCamera(editor::EditorCamera* camera)
        {
            editorCameraWindow.setEditorCamera(camera);
        }

    private:
        void subscribeToEvents();
        void onSceneCleared();
    };
}
