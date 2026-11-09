#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <istream>
#include <vector>
#include "Types.hpp"

namespace resource
{
    // Header-level facts about a .vfAnim clip (VK-1656), read without loading its keys.
    struct AnimationClipHeader
    {
        bool success = false;
        std::string error; // why the clip could not be read (success == false)
        std::string name;
        float durationTicks = 0.0f;
        // The rate the runtime plays the clip at: 24 when the file stores <= 0
        // (the AnimationEvaluator / AnimatorStateMachine fallback).
        float ticksPerSecond = 24.0f;
        float durationSeconds = 0.0f;
        std::vector<std::string> channelNames; // one per animated node, file order
        uint32_t eventCount = 0;               // events loadAnimation() would load
    };

    class AnimationResource
    {
    public:
        static AnimationData loadAnimation(std::string_view path);

        // Reads the header, name, duration, channel names and event count of a .vfAnim
        // from disk, seeking past the key arrays. Same file-type/version gate as
        // loadAnimation(); truncated or corrupt files report an error instead of
        // partial data.
        static AnimationClipHeader readClipSummary(std::string_view path);

        // Returns the file offset where event data begins (after channels).
        // Used by AnimationEventWriter to avoid re-parsing the entire file.
        static std::streampos getEventDataOffset(std::string_view path);

    private:
        static std::string readString(std::istream& file);
        static void skipString(std::istream& file);
    };
}
