#pragma once
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace events
{
    class EventDispatcher;
}

namespace services
{
    struct StreamingZone
    {
        uint32_t id;
        glm::vec3 boundsMin;
        glm::vec3 boundsMax;
        std::string scenePath;
        std::string sceneName;
        bool isLoaded = false;
    };

    class StreamingZoneManager
    {
    private:
        std::unordered_map<uint32_t, StreamingZone> zones;
        uint32_t nextZoneId = 1;

    public:
        StreamingZoneManager() = default;
        ~StreamingZoneManager() = default;

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        uint32_t addZone(const glm::vec3& boundsMin, const glm::vec3& boundsMax, const std::string& scenePath);
        bool removeZone(uint32_t zoneId);
        void clear();
        bool isScenePathLoaded(const std::string& scenePath) const;

        void update();

    private:
        bool isPointInAABB(const glm::vec3& point, const glm::vec3& min, const glm::vec3& max) const;
        glm::vec3 getCameraPosition() const;
        std::string generateSceneName(const std::string& scenePath, uint32_t zoneId) const;
    };
}
