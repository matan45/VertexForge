#pragma once

#include "resource/Types.hpp"
#include <string>

namespace services { struct PreviewInstanceId; }

namespace windows::animation
{
    class AnimationInfoPanel
    {
    public:
        struct State
        {
            std::string animationPath;
            std::string meshPath;
            resource::AnimationData* animationData = nullptr;
            bool animationLoaded = false;
            bool loadFailed = false;
            bool meshLoadedInPreview = false;
            bool animationLoadedInPreview = false;
            bool previewInitialized = false;
            bool isPlaying = false;
            bool isLooping = true;
            float playbackSpeed = 1.0f;
        };

        void draw(State& state, const services::PreviewInstanceId& instanceId);
        void drawLoadingIndicator(const std::string& loadingStatus);

    private:
        void drawMeshFileInput(State& state, const services::PreviewInstanceId& instanceId);
        void drawPlaybackControls(State& state, const services::PreviewInstanceId& instanceId);
    };
}
