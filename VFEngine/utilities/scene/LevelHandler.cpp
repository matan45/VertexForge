#include "LevelHandler.hpp"

namespace scene
{
    std::shared_ptr<Level> LevelHandler::getInstance()
    {
        if (!level)
        {
            level = std::make_shared<Level>();
        }
        return level;
    }

    void LevelHandler::setInstance(const std::shared_ptr<Level>& newLevel)
    {
        level = newLevel;
    }

    void LevelHandler::update()
    {
        if(level)
        {
            level->update();
        }
    }
}
