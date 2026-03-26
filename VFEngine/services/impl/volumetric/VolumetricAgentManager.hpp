#pragma once

#include "../../providers/volumetric/IVolumetricNavProvider.hpp"
#include "../../data/EntityHandle.hpp"
#include "navigation/volumetric/VolumetricTypes.hpp"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace services
{
    class VolumetricAgentManager
    {
    public:
        explicit VolumetricAgentManager(IVolumetricNavProvider* provider);

        void addAgent(EntityHandle entity);
        void removeAgent(EntityHandle entity);
        void setDestination(EntityHandle entity, const glm::vec3& target);
        void stopAgent(EntityHandle entity);
        void updatePositions(float deltaTime);
        void clear();

    private:
        IVolumetricNavProvider* provider;

        struct AgentState
        {
            volumetric::VolumePath path;
            size_t currentIndex = 0;
            glm::vec3 velocity{0.0f};
        };

        std::unordered_map<uint64_t, AgentState> agents;

        static constexpr float ARRIVAL_DISTANCE = 1.0f;
    };
}
