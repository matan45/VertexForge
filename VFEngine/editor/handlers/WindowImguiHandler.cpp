#include "WindowImguiHandler.hpp"
#include "../windows/ConsoleLog.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "../windows/SceneHierarchyPanel.hpp"
#include "../windows/EntityDetailsPanel.hpp"
#include "../windows/ViewPort.hpp"
#include "../windows/MainImguiWindow.hpp"
#include "../windows/ContentBrowser.hpp"
#include "../windows/FolderStructureWindow.hpp"
#include "../windows/ImportProgressWindow.hpp"

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
    }

    void WindowImguiHandler::cleanUp() const
    {
        controllers::imguiHandler::ImguiWindowHandler::cleanUp();
    }
}
