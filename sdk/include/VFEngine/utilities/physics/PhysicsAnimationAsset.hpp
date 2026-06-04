#pragma once

#include "../types/PhysicsAnimationTypes.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string_view>
#include <optional>

namespace physics
{
    inline constexpr const char* PHYSICS_ANIM_FORMAT_VERSION = "1.0";

    class PhysicsAnimationAsset
    {
    public:
        static std::optional<types::PhysicsAnimationConfig> load(std::string_view path);
        static bool save(std::string_view path, const types::PhysicsAnimationConfig& config);

    private:
        static nlohmann::json serializeConfig(const types::PhysicsAnimationConfig& config);
        static types::PhysicsAnimationConfig deserializeConfig(const nlohmann::json& j);

        static std::string colliderShapeToString(types::ColliderShape shape);
        static types::ColliderShape stringToColliderShape(const std::string& str);
        static std::string modeToString(types::PhysicsAnimationMode mode);
        static types::PhysicsAnimationMode stringToMode(const std::string& str);
    };
}
