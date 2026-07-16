#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <memory>
#include <optional>

namespace scene {
    class SceneGraphSystem;
}

namespace events {
    class EventDispatcher;
}

namespace services {

    class AudioComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit AudioComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        // 2D Audio Source Operations (streaming, for background music)
        bool addAudioSource2DComponent(EntityHandle entity);
        bool removeAudioSource2DComponent(EntityHandle entity);
        bool hasAudioSource2DComponent(EntityHandle entity) const;
        std::optional<AudioSource2DData> getAudioSource2DData(EntityHandle entity) const;
        bool setAudioSource2DData(EntityHandle entity, const AudioSource2DData& audioData);

        // 3D Audio Source Operations (cached, for spatial sound effects)
        bool addAudioSource3DComponent(EntityHandle entity);
        bool removeAudioSource3DComponent(EntityHandle entity);
        bool hasAudioSource3DComponent(EntityHandle entity) const;
        std::optional<AudioSource3DData> getAudioSource3DData(EntityHandle entity) const;
        bool setAudioSource3DData(EntityHandle entity, const AudioSource3DData& audioData);
        bool setAudioSource3DDistances(EntityHandle entity, float minDistance, float maxDistance);

        // Reverb Zone Operations
        bool addReverbZoneComponent(EntityHandle entity);
        bool removeReverbZoneComponent(EntityHandle entity);
        bool hasReverbZoneComponent(EntityHandle entity) const;
        std::optional<ReverbZoneData> getReverbZoneData(EntityHandle entity) const;
        bool setReverbZoneData(EntityHandle entity, const ReverbZoneData& data);

    };

}
