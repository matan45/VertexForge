#pragma once
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include "Types.hpp"

namespace resource
{
    class SkeletonResource
    {
    public:
        // Load skeleton from .vfSkeleton file
        static SkeletonData loadSkeleton(std::string_view path);

        // Load skeleton with caching (returns shared pointer)
        static std::shared_ptr<SkeletonData> loadSkeletonCached(std::string_view path);

        // Clear the skeleton cache
        static void clearCache();

        // Validate skeleton file header
        static bool validateFile(std::string_view path);

    private:
        static std::string readString(std::ifstream& file);
        static glm::mat4 readMatrix(std::ifstream& file);

        // Cache for loaded skeletons
        static std::unordered_map<std::string, std::shared_ptr<SkeletonData>> skeletonCache;
    };
}
