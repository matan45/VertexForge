#include "WindowImguiHandler.hpp"
#include "../windows/ConsoleLog.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "../windows/scene/SceneHierarchyPanel.hpp"
#include "../windows/scene/EntityDetailsPanel.hpp"
#include "../windows/viewport/ViewPort.hpp"
#include "../windows/MainImguiWindow.hpp"
#include "../windows/contentbrowser/ContentBrowser.hpp"
#include "../windows/scene/FolderStructureWindow.hpp"
#include "../windows/import/ImportProgressWindow.hpp"
#include "../windows/import/ExportProgressWindow.hpp"
#include "../windows/import/FileOperationProgressWindow.hpp"
#include "../windows/import/SceneLoadProgressWindow.hpp"
#include "../windows/config/NavmeshWindow.hpp"
#include "../windows/AssetLifecycleWindow.hpp"

namespace handlers
{
    void WindowImguiHandler::init()
    {
        // Create windows
        auto mainWindow = std::make_shared<windows::MainImguiWindow>();
        auto viewPort = std::make_shared<windows::ViewPort>();
        
        // Connect editor camera from ViewPort to MainImguiWindow
        mainWindow->setEditorCamera(viewPort->getEditorCamera());

        // Add all windows
        controllers::imguiHandler::ImguiWindowHandler::add(mainWindow);
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::ConsoleLog>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::ContentBrowser>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::FolderStructureWindow>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::SceneHierarchyPanel>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::EntityDetailsPanel>());
        controllers::imguiHandler::ImguiWindowHandler::add(viewPort);
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::ImportProgressWindow>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::ExportProgressWindow>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::FileOperationProgressWindow>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::SceneLoadProgressWindow>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::NavmeshWindow>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::AssetLifecycleWindow>());
    }

    void WindowImguiHandler::cleanUp() const
    {
        controllers::imguiHandler::ImguiWindowHandler::cleanUp();
    }
}
