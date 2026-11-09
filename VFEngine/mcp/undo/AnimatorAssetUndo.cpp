#include "AnimatorAssetUndo.hpp"
#include "../tools/AnimationToolSupport.hpp"
#include "../tools/PathSandbox.hpp"

#include "events/EventDispatcher.hpp"
#include "events/animation/AnimationAuthoringEvents.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace mcp::undo
{
    AnimatorAssetUndo::AnimatorAssetUndo(std::string path, animator::AnimatorData before, animator::AnimatorData after,
                                         uint64_t contentHash)
        : path(std::move(path)), before(std::move(before)), after(std::move(after)), contentHash(contentHash)
    {
        name = tools::pathToUtf8(std::filesystem::path(this->path).stem());
        footprint = sizeof(*this) + this->path.capacity() + name.capacity() +
                    tools::anim::animatorDataBytes(this->before) + tools::anim::animatorDataBytes(this->after);
    }

    void AnimatorAssetUndo::write(const animator::AnimatorData& data)
    {
        events::animationAuthoring::SaveAnimatorAssetCommand command;
        command.path = path;
        command.data = data;
        command.expectedContentHash = contentHash;
        const services::AnimatorAssetSaveResult result = events::EventDispatcher::instance().execute(command);
        if (!result.success)
        {
            throw std::runtime_error("rewriting the animator '" + path + "' was refused: " + result.error);
        }
        contentHash = result.contentHash;
    }

    void AnimatorAssetUndo::execute() { write(after); }
    void AnimatorAssetUndo::undo() { write(before); }

    std::string AnimatorAssetUndo::getDescription() const
    {
        return "MCP: Edit animator " + name;
    }

    size_t AnimatorAssetUndo::getMemoryFootprint() const
    {
        return footprint;
    }
}
