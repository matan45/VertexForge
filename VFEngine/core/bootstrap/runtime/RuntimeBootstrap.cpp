#include "RuntimeBootstrap.hpp"
#include "../../controllers/CoreInterface.hpp"
#include "../../../window/window/Window.hpp"
#include "scene/LevelHandler.hpp"

namespace core
{
    void RuntimeBootstrap::run() const
    {
        coreInterface->run();
    }

    window::Window* RuntimeBootstrap::getWindow()
    {
        return coreInterface ? coreInterface->getWindow() : nullptr;
    }

    void RuntimeBootstrap::setWindowTitle(const std::string& title)
    {
        if (auto* win = getWindow())
        {
            win->setTitle(title);
        }
    }

    void RuntimeBootstrap::setWindowIcon(const std::string& iconPath)
    {
        if (auto* win = getWindow())
        {
            win->setWindowIcon(iconPath);
        }
    }

    std::shared_ptr<scene::SceneGraphSystem> RuntimeBootstrap::getSceneGraphSystem()
    {
        auto level = scene::LevelHandler::getInstance();
        return level ? level->getSceneGraphSystem() : nullptr;
    }

    void RuntimeBootstrap::triggerResize()
    {
        if (coreInterface)
        {
            coreInterface->triggerResize();
        }
    }
}
