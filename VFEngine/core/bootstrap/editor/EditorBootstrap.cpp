#include "EditorBootstrap.hpp"
#include "../../controllers/CoreInterface.hpp"
#include "scene/LevelHandler.hpp"

namespace core
{
    void EditorBootstrap::run() const
    {
        coreInterface->run();
    }

    window::Window* EditorBootstrap::getWindow()
    {
        return coreInterface ? coreInterface->getWindow() : nullptr;
    }

    std::shared_ptr<scene::SceneGraphSystem> EditorBootstrap::getSceneGraphSystem()
    {
        auto level = scene::LevelHandler::getInstance();
        return level ? level->getSceneGraphSystem() : nullptr;
    }

    void EditorBootstrap::triggerResize()
    {
        if (coreInterface)
        {
            coreInterface->triggerResize();
        }
    }
}
