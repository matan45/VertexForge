#pragma once

#include "animator/AnimatorTypes.hpp"
#include "data/UndoTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace mcp::undo
{
    // VK-1656: an animation_set_state_machine edit of an existing .vfAnimator. Undo / redo rewrite the
    // whole file through SaveAnimatorAssetCommand, guarded by the hash of the bytes the previous write
    // left on disk: when the file changed since (an Animator Editor save, another edit, a delete), the
    // step throws instead of overwriting that work, and McpBatchUndoCommand logs it and consumes the entry.
    // Each successful write publishes AssetSavedNotification, so live animators and an open Animator
    // Editor reload exactly as after the original edit.
    class AnimatorAssetUndo : public services::IUndoableCommand
    {
    public:
        // `path` is the absolute engine path; `contentHash` hashes the bytes the edit itself wrote.
        AnimatorAssetUndo(std::string path, animator::AnimatorData before, animator::AnimatorData after,
                          uint64_t contentHash);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void write(const animator::AnimatorData& data);

        std::string path;
        std::string name; // the file name without extension, for the description
        animator::AnimatorData before;
        animator::AnimatorData after;
        uint64_t contentHash; // the file's bytes after the most recent write
        size_t footprint;
    };
}
