#pragma once
#include <memory>
#include "Level.hpp"

namespace scene
{
    class LevelHandler
    {
    private:
        static inline std::shared_ptr<Level> level = nullptr;

    public:
        explicit LevelHandler() = default;
        ~LevelHandler() = default;

        static std::shared_ptr<Level> getInstance();
        static void setInstance(const std::shared_ptr<Level>& newLevel);
        static void update();

        static bool isWorldMode()
        {
            return level && level->isWorldLevel();
        }
    };
}
