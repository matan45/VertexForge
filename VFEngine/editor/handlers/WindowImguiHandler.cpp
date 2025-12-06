#include "WindowImguiHandler.hpp"
#include "../windows/ConsoleLog.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "../windows/SceneGraph.hpp"
#include "../windows/ViewPort.hpp"
#include "../windows/MainImguiWindow.hpp"
#include "../windows/ContentBrowser.hpp"

namespace handlers
{
    void WindowImguiHandler::init()
    {
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::MainImguiWindow>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::ConsoleLog>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::ContentBrowser>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::SceneGraph>());
        controllers::imguiHandler::ImguiWindowHandler::add(std::make_shared<windows::ViewPort>());
    }

    void WindowImguiHandler::cleanUp() const
    {
        controllers::imguiHandler::ImguiWindowHandler::cleanUp();
    }
}
