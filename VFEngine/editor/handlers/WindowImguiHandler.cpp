#include "WindowImguiHandler.hpp"
#include "../windows/ConsoleLog.hpp"

namespace handlers
{
    void WindowImguiHandler::init()
    {
        // Create windows - all use services via ServiceLocator
        mainImguiWindow = std::make_shared<windows::MainImguiWindow>();
        contentBrowserWindow = std::make_shared<windows::ContentBrowser>();
        sceneGraphWindow = std::make_shared<windows::SceneGraph>();
        viewPortWindow = std::make_shared<windows::ViewPort>();

        controllers::imguiHandler::ImguiWindowHandler::add(mainImguiWindow);
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::ConsoleLog>());
        controllers::imguiHandler::ImguiWindowHandler::add(contentBrowserWindow);
        controllers::imguiHandler::ImguiWindowHandler::add(sceneGraphWindow);
        controllers::imguiHandler::ImguiWindowHandler::add(viewPortWindow);
    }

    void WindowImguiHandler::cleanUp() const
    {
        controllers::imguiHandler::ImguiWindowHandler::cleanUp();
    }
}
