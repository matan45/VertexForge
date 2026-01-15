#pragma once
#include <string>
#include <string_view>
#include <memory>
#include "Types.hpp"

namespace resource
{
    class AnimationResource
    {
    public:
        // Load animation from .vfAnim file
        // If skeleton reference exists, loads skeleton from .vfSkeleton file
        static AnimationData loadAnimation(std::string_view path);

        // Load animation with separate skeleton pointer
        // Returns the animation data and sets skeletonOut if skeleton reference exists
        static AnimationData loadAnimationWithSkeleton(std::string_view path,
                                                       std::shared_ptr<SkeletonData>& skeletonOut);

        // Validate animation file header
        static bool validateFile(std::string_view path);

    private:
        static std::string readString(std::ifstream& file);
        static glm::mat4 readMatrix(std::ifstream& file);
    };
}
