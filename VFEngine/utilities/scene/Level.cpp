#include "Level.hpp"

namespace scene
{
    Level::Level()
    {
        sceneGraphSystem = std::make_shared<SceneGraphSystem>();
    }

    void Level::setName(const std::string& _name)
    {
        name = _name;
    }

    std::string Level::getName()
    {
        return name;
    }

    void Level::setSceneGraphSystem(const std::shared_ptr<SceneGraphSystem>& _sceneGraphSystem)
    {
        sceneGraphSystem = _sceneGraphSystem;
    }

    std::shared_ptr<SceneGraphSystem> Level::getSceneGraphSystem()
    {
        return this->sceneGraphSystem;
    }

    void Level::update() const
    {
        sceneGraphSystem->updateWorldTransforms();
        sceneGraphSystem->updateCamera();
    }
}
