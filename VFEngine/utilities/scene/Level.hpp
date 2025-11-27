#pragma once
#include <memory>
#include <string>

#include "SceneGraphSystem.hpp"

namespace scene
{
    class Level
    {
    private:
        std::string name;
        std::shared_ptr<SceneGraphSystem> sceneGraphSystem;

    public:
        Level();
        ~Level() = default;

        void setName(const std::string& name);
        std::string getName();
        void setSceneGraphSystem(const std::shared_ptr<SceneGraphSystem>& sceneGraphSystem);
        std::shared_ptr<SceneGraphSystem> getSceneGraphSystem();

        void update() const;
    };
}
