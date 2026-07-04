#pragma once

#include "VFXTypes.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace vfx
{
    enum class VFXEventType : uint8_t
    {
        OnSpawn = 0,
        OnDeath = 1,
        OnCollision = 2,
        OnLifetimeThreshold = 3
    };

    inline constexpr uint32_t VFX_EVENT_TYPE_COUNT = 4;

    namespace EventFlags
    {
        inline constexpr uint32_t OnSpawn = 1 << 0;
        inline constexpr uint32_t OnDeath = 1 << 1;
        inline constexpr uint32_t OnCollision = 1 << 2;
        inline constexpr uint32_t OnLifetimeThreshold = 1 << 3;
    }

    namespace EventDefaults
    {
        inline constexpr int32_t SPAWN_COUNT = 1;
        inline constexpr int32_t MIN_SPAWN_COUNT = 1;
        inline constexpr int32_t MAX_SPAWN_COUNT = 8;
        inline constexpr float PROBABILITY = 1.0f;
        inline constexpr float INHERIT_VELOCITY_SCALE = 0.0f;
        inline constexpr float MAX_INHERIT_VELOCITY_SCALE = 2.0f;
        inline constexpr bool INHERIT_COLOR = false;
        inline constexpr bool INHERIT_SIZE = false;
        inline constexpr float LIFETIME_THRESHOLD = 0.5f;
    }

    struct VFXEventTypeConfig
    {
        bool enabled = false;
        std::string vfxPath;
        int32_t spawnCount = EventDefaults::SPAWN_COUNT;
        float probability = EventDefaults::PROBABILITY;
        float inheritVelocityScale = EventDefaults::INHERIT_VELOCITY_SCALE;
        bool inheritColor = EventDefaults::INHERIT_COLOR;
        bool inheritSize = EventDefaults::INHERIT_SIZE;
    };

    struct VFXEventConfig
    {
        std::array<VFXEventTypeConfig, VFX_EVENT_TYPE_COUNT> types{};

        float lifetimeThreshold = EventDefaults::LIFETIME_THRESHOLD;

        uint32_t toEventFlags() const
        {
            uint32_t flags = 0;
            if (types[static_cast<size_t>(VFXEventType::OnSpawn)].enabled) flags |= EventFlags::OnSpawn;
            if (types[static_cast<size_t>(VFXEventType::OnDeath)].enabled) flags |= EventFlags::OnDeath;
            if (types[static_cast<size_t>(VFXEventType::OnCollision)].enabled) flags |= EventFlags::OnCollision;
            if (types[static_cast<size_t>(VFXEventType::OnLifetimeThreshold)].enabled) flags |= EventFlags::OnLifetimeThreshold;
            return flags;
        }
    };

    const char* eventTypeToString(VFXEventType type);
    VFXEventType stringToEventType(const std::string& str);

    inline uint32_t eventTypeIndex(VFXEventType type)
    {
        return static_cast<uint32_t>(type);
    }

    inline const char* eventTypeKeyPrefix(VFXEventType type)
    {
        switch (type)
        {
        case VFXEventType::OnSpawn:             return "eventOnSpawn";
        case VFXEventType::OnDeath:             return "eventOnDeath";
        case VFXEventType::OnCollision:         return "eventOnCollision";
        case VFXEventType::OnLifetimeThreshold: return "eventOnLifetimeThreshold";
        default:                                return "eventOnSpawn";
        }
    }

    inline std::string eventPropName(VFXEventType type, const char* suffix)
    {
        return std::string(eventTypeKeyPrefix(type)) + suffix;
    }

    inline bool getEventBoolProp(const VFXNode& node, const std::string& name, bool defaultValue)
    {
        auto it = node.properties.find(name);
        if (it != node.properties.end())
            if (auto* val = std::get_if<bool>(&it->second.value))
                return *val;
        return defaultValue;
    }

    inline int32_t getEventIntProp(const VFXNode& node, const std::string& name, int32_t defaultValue)
    {
        auto it = node.properties.find(name);
        if (it != node.properties.end())
            if (auto* val = std::get_if<int32_t>(&it->second.value))
                return *val;
        return defaultValue;
    }

    inline float getEventFloatProp(const VFXNode& node, const std::string& name, float defaultValue)
    {
        auto it = node.properties.find(name);
        if (it != node.properties.end())
            if (auto* val = std::get_if<float>(&it->second.value))
                return *val;
        return defaultValue;
    }

    inline std::string getEventStringProp(const VFXNode& node, const std::string& name, const std::string& defaultValue)
    {
        auto it = node.properties.find(name);
        if (it != node.properties.end())
            if (auto* val = std::get_if<std::string>(&it->second.value))
                return *val;
        return defaultValue;
    }

    inline VFXEventConfig loadEventConfigFromNode(const VFXNode& node)
    {
        VFXEventConfig config;
        for (uint32_t i = 0; i < VFX_EVENT_TYPE_COUNT; ++i)
        {
            const auto type = static_cast<VFXEventType>(i);
            auto& tc = config.types[i];
            tc.enabled = getEventBoolProp(node, eventPropName(type, "Enabled"), false);
            tc.vfxPath = getEventStringProp(node, eventPropName(type, "VFX"), "");
            tc.spawnCount = std::clamp(getEventIntProp(node, eventPropName(type, "Count"),
                                                       EventDefaults::SPAWN_COUNT),
                                       EventDefaults::MIN_SPAWN_COUNT,
                                       EventDefaults::MAX_SPAWN_COUNT);
            tc.probability = std::clamp(getEventFloatProp(node, eventPropName(type, "Probability"),
                                                          EventDefaults::PROBABILITY),
                                        0.0f, 1.0f);
            tc.inheritVelocityScale = std::clamp(getEventFloatProp(node, eventPropName(type, "VelInherit"),
                                                                   EventDefaults::INHERIT_VELOCITY_SCALE),
                                                 0.0f, EventDefaults::MAX_INHERIT_VELOCITY_SCALE);
            tc.inheritColor = getEventBoolProp(node, eventPropName(type, "InheritColor"),
                                               EventDefaults::INHERIT_COLOR);
            tc.inheritSize = getEventBoolProp(node, eventPropName(type, "InheritSize"),
                                              EventDefaults::INHERIT_SIZE);
        }

        config.lifetimeThreshold = std::clamp(
            getEventFloatProp(node, "eventLifetimeThreshold", EventDefaults::LIFETIME_THRESHOLD),
            0.0f, 1.0f);
        return config;
    }

    inline void storeEventConfigToNode(VFXNode& node, const VFXEventConfig& config)
    {
        for (uint32_t i = 0; i < VFX_EVENT_TYPE_COUNT; ++i)
        {
            const auto type = static_cast<VFXEventType>(i);
            const auto& tc = config.types[i];

            const std::string enabledKey = eventPropName(type, "Enabled");
            node.properties[enabledKey] = VFXProperty{
                enabledKey, VFXPropertyType::Bool, tc.enabled, 0.0f, 1.0f};

            const std::string vfxKey = eventPropName(type, "VFX");
            node.properties[vfxKey] = VFXProperty{
                vfxKey, VFXPropertyType::String, tc.vfxPath, 0.0f, 0.0f};

            const std::string countKey = eventPropName(type, "Count");
            node.properties[countKey] = VFXProperty{
                countKey, VFXPropertyType::Int,
                std::clamp(tc.spawnCount, EventDefaults::MIN_SPAWN_COUNT, EventDefaults::MAX_SPAWN_COUNT),
                static_cast<float>(EventDefaults::MIN_SPAWN_COUNT),
                static_cast<float>(EventDefaults::MAX_SPAWN_COUNT)};

            const std::string probabilityKey = eventPropName(type, "Probability");
            node.properties[probabilityKey] = VFXProperty{
                probabilityKey, VFXPropertyType::Float,
                std::clamp(tc.probability, 0.0f, 1.0f), 0.0f, 1.0f};

            const std::string velocityKey = eventPropName(type, "VelInherit");
            node.properties[velocityKey] = VFXProperty{
                velocityKey, VFXPropertyType::Float,
                std::clamp(tc.inheritVelocityScale, 0.0f, EventDefaults::MAX_INHERIT_VELOCITY_SCALE),
                0.0f, EventDefaults::MAX_INHERIT_VELOCITY_SCALE};

            const std::string inheritColorKey = eventPropName(type, "InheritColor");
            node.properties[inheritColorKey] = VFXProperty{
                inheritColorKey, VFXPropertyType::Bool, tc.inheritColor, 0.0f, 1.0f};

            const std::string inheritSizeKey = eventPropName(type, "InheritSize");
            node.properties[inheritSizeKey] = VFXProperty{
                inheritSizeKey, VFXPropertyType::Bool, tc.inheritSize, 0.0f, 1.0f};
        }

        node.properties["eventLifetimeThreshold"] = VFXProperty{
            "eventLifetimeThreshold", VFXPropertyType::Float,
            std::clamp(config.lifetimeThreshold, 0.0f, 1.0f), 0.0f, 1.0f};
    }

    inline float eventProbabilityRoll(uint32_t seed, uint32_t eventIndex, uint32_t eventType)
    {
        uint32_t state = seed;
        state ^= eventIndex * 747796405u + 2891336453u;
        state ^= eventType * 277803737u + 0x9e3779b9u;
        state = state * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        uint32_t hash = (word >> 22u) ^ word;
        return static_cast<float>(hash) / 4294967296.0f;
    }

    template <typename Rand01>
    uint32_t evaluateEventSpawnCount(const VFXEventTypeConfig& config,
                                     uint32_t capHeadroom,
                                     Rand01&& rand01)
    {
        if (!config.enabled || config.vfxPath.empty() || capHeadroom == 0 || config.probability <= 0.0f)
            return 0;

        if (config.probability < 1.0f && rand01() > config.probability)
            return 0;

        const int32_t clampedCount = std::clamp(config.spawnCount,
                                                EventDefaults::MIN_SPAWN_COUNT,
                                                EventDefaults::MAX_SPAWN_COUNT);
        return std::min(static_cast<uint32_t>(clampedCount), capHeadroom);
    }

    struct VFXEventSpawnInput
    {
        uint32_t eventType = 0;
        uint32_t parentKey = 0;
        VFXEventTypeConfig config;
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 color{1.0f};
        float size = 1.0f;
    };

    struct VFXSpawnRequestOut
    {
        uint32_t eventType = 0;
        uint32_t parentKey = 0;
        std::string vfxPath;
        glm::vec3 position{0.0f};
        std::optional<glm::vec3> inheritedVelocity;
        std::optional<glm::vec4> startColorMultiplier;
        std::optional<float> startSizeMultiplier;
    };

    template <typename Rand01, typename Headroom>
    std::vector<VFXSpawnRequestOut> evaluateEventSpawns(const std::vector<VFXEventSpawnInput>& events,
                                                        Rand01&& rand01,
                                                        Headroom&& capHeadroom)
    {
        std::vector<VFXSpawnRequestOut> out;
        std::unordered_map<uint32_t, uint32_t> remainingByParent;
        remainingByParent.reserve(events.size());

        for (const auto& event : events)
        {
            auto [it, inserted] = remainingByParent.emplace(event.parentKey, 0u);
            if (inserted)
                it->second = capHeadroom(event.parentKey);

            uint32_t count = evaluateEventSpawnCount(event.config, it->second,
                [&]() { return rand01(event); });
            it->second -= count;

            for (uint32_t i = 0; i < count; ++i)
            {
                VFXSpawnRequestOut req;
                req.eventType = event.eventType;
                req.parentKey = event.parentKey;
                req.vfxPath = event.config.vfxPath;
                req.position = event.position;
                if (event.config.inheritVelocityScale > 0.0f)
                    req.inheritedVelocity = event.velocity * event.config.inheritVelocityScale;
                if (event.config.inheritColor)
                    req.startColorMultiplier = glm::vec4(event.color, 1.0f);
                if (event.config.inheritSize)
                    req.startSizeMultiplier = event.size;
                out.push_back(std::move(req));
            }
        }

        return out;
    }
}
