#pragma once
#include <fstream>
#include <functional>
#include <string>
#include "config/Config.hpp"
#include "resource/Types.hpp"

struct aiScene;
struct aiAnimation;

namespace types
{
    using AnimationProgressCallback = std::function<void(float progress)>;

    class Animation
    {
    public:

        void loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                          std::string_view location,
                          AnimationProgressCallback progressCallback = nullptr) const;

    private:
        void extractFromScene(const aiScene* scene, std::string_view fileName,
                             std::string_view location,
                             AnimationProgressCallback progressCallback = nullptr) const;

        resource::AnimationData extractAnimation(const aiAnimation* anim) const;

        void saveToFile(std::string_view location, std::string_view baseName,
                        const resource::AnimationData& animData) const;

        void writeString(std::ofstream& file, const std::string& str) const;
        void writeChannels(std::ofstream& file, const std::vector<resource::BoneAnimation>& channels) const;

        static std::string sanitizeAnimationName(const std::string& name);
    };
}
