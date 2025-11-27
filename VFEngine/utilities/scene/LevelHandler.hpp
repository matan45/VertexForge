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
        LevelHandler() = default;
        ~LevelHandler() = default;

        static std::shared_ptr<Level> getInstance();
        static void setInstance(const std::shared_ptr<Level>& newLevel);
        static void update();
    };
}
