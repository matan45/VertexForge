#pragma once
#include <string_view>
#include <fstream>
#include "Types.hpp"

namespace resource
{
    class AnimationResource
    {
    public:
        static AnimationData loadAnimation(std::string_view path);

        // Returns the file offset where event data begins (after channels).
        // Used by AnimationEventWriter to avoid re-parsing the entire file.
        static std::streampos getEventDataOffset(std::string_view path);

    private:
        static std::string readString(std::ifstream& file);
        static void skipString(std::ifstream& file);
    };
}
