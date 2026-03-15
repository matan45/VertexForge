#pragma once
#include "types/AudioEffectTypes.hpp"
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <vector>
#include <cstdint>

namespace core::audio
{
    class ReverbZoneManager
    {
    public:
        void init(int reservedSendIndex);
        void cleanUp();

        void update(const glm::vec3& listenerPos, entt::registry& registry);

        void routeSource(uint32_t sourceId);
        void unrouteSource(uint32_t sourceId);

        int getReservedSendIndex() const { return reservedSend; }
        bool isEnabled() const { return enabled; }

    private:
        struct ActiveZone
        {
            int priority = 0;
            float blendWeight = 0.0f;
            types::ReverbParams params;
        };

        types::ReverbParams lerpParams(const types::ReverbParams& a, const types::ReverbParams& b, float t) const;
        void applyReverbToEffect(const types::ReverbParams& params);

        uint32_t effectObject = 0;
        uint32_t auxSlot = 0;
        int reservedSend = 0;
        bool enabled = false;
        bool reverbActive = false;

        std::vector<uint32_t> routedSources;
    };
}
