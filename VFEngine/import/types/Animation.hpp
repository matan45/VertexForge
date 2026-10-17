#pragma once
#include <fstream>
#include <functional>
#include <string>
#include <vector>
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
        // Writes one <fileName>_<take>.vfAnim per animation in the file. outWrittenFiles,
        // when non-null, receives the path of every .vfAnim actually written so the
        // importer can report it (result + .vfmeta).
        void loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                          std::string_view location,
                          AnimationProgressCallback progressCallback = nullptr,
                          std::vector<std::string>* outWrittenFiles = nullptr) const;

    private:
        void extractFromScene(const aiScene* scene, std::string_view fileName,
                             std::string_view location,
                             AnimationProgressCallback progressCallback = nullptr,
                             std::vector<std::string>* outWrittenFiles = nullptr) const;

        resource::AnimationData extractAnimation(const aiAnimation* anim) const;

        // Returns the written file's path, or an empty string when it could not be written.
        std::string saveToFile(std::string_view location, std::string_view baseName,
                               const resource::AnimationData& animData) const;

        void writeString(std::ofstream& file, const std::string& str) const;
        void writeChannels(std::ofstream& file, const std::vector<resource::BoneAnimation>& channels) const;

        static std::string sanitizeAnimationName(const std::string& name);
    };
}
