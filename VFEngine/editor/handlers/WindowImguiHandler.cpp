#include "WindowImguiHandler.hpp"
#include "../windows/ConsoleLog.hpp"
#include "../windows/MainImguiWindow.hpp"
#include "../windows/ContentBrowser.hpp"
#include "../windows/ViewPort.hpp"


namespace handlers
{
    WindowImguiHandler::WindowImguiHandler(controllers::OffScreen& offscreen,
                                           controllers::CoreInterface& coreInterface) : offscreen{offscreen},
        coreInterface{coreInterface}
    {
        // Get the Level's SceneGraphSystem so everything shares the same instance
        auto level = scene::LevelHandler::getInstance();
        sceneGraphSystem = level->getSceneGraphSystem();
    }

    void WindowImguiHandler::init()
    {
        // Create windows and store references for service mode enablement
        mainImguiWindow = std::make_shared<windows::MainImguiWindow>(coreInterface, offscreen, sceneGraphSystem);
        contentBrowserWindow = std::make_shared<windows::ContentBrowser>();
        sceneGraphWindow = std::make_shared<windows::SceneGraph>(sceneGraphSystem);
        viewPortWindow = std::make_shared<windows::ViewPort>(offscreen, sceneGraphSystem);

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

    void WindowImguiHandler::enableServiceMode()
    {
        if (mainImguiWindow) {
            mainImguiWindow->enableServiceMode();
        }
        if (contentBrowserWindow) {
            contentBrowserWindow->enableServiceMode();
        }
        if (sceneGraphWindow) {
            sceneGraphWindow->enableServiceMode();
        }
        if (viewPortWindow) {
            viewPortWindow->enableServiceMode();
        }
    }
}
