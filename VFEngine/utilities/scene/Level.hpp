#pragma once
#include <memory>
#include <optional>
#include <string>

#include "SceneGraphSystem.hpp"
#include "../world/WorldDefinition.hpp"

namespace scene
{
    class Level
    {
    private:
        std::string name;
        std::shared_ptr<SceneGraphSystem> sceneGraphSystem;
        std::optional<world::WorldDefinition> worldDefinition;

    public:
        explicit Level();
        ~Level() = default;

        void setName(const std::string& name);
        std::string getName();
        void setSceneGraphSystem(const std::shared_ptr<SceneGraphSystem>& sceneGraphSystem);
        std::shared_ptr<SceneGraphSystem> getSceneGraphSystem();

        void setWorldDefinition(const world::WorldDefinition& def) { worldDefinition = def; }
        void clearWorldDefinition() { worldDefinition.reset(); }
        [[nodiscard]] bool isWorldLevel() const { return worldDefinition.has_value(); }
        [[nodiscard]] const std::optional<world::WorldDefinition>& getWorldDefinition() const { return worldDefinition; }

        void update() const;
    };
}
