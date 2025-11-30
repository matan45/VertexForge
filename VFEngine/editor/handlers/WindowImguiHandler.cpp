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

    void WindowImguiHandler::init() const
    {
        controllers::imguiHandler::ImguiWindowHandler::add(
            std::make_shared<windows::MainImguiWindow>(coreInterface, offscreen, sceneGraphSystem));
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::ConsoleLog>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::ContentBrowser>());
        controllers::imguiHandler::ImguiWindowHandler::add(
            std::make_shared<windows::SceneGraph>(sceneGraphSystem));
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::ViewPort>(offscreen, sceneGraphSystem));
    }

    void WindowImguiHandler::cleanUp() const
    {
        controllers::imguiHandler::ImguiWindowHandler::cleanUp();
    }
}
