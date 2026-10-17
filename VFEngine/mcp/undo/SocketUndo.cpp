#include "SocketUndo.hpp"
#include "McpUndoCommands.hpp"
#include "../tools/AnimationRigSupport.hpp"

#include "events/EventDispatcher.hpp"
#include "events/physics/SocketEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <stdexcept>
#include <utility>

namespace mcp::undo
{
    namespace
    {
        std::string idText(uint32_t recordedId)
        {
            return std::to_string(recordedId);
        }

        services::EntityHandle requireLive(uint32_t recordedId)
        {
            const services::EntityHandle handle = currentHandle(recordedId);
            if (!tools::rig::isLiveEntity(handle))
            {
                throw std::runtime_error("entity " + idText(recordedId) + " no longer exists");
            }
            return handle;
        }

        void detach(const services::EntityHandle& child)
        {
            events::socket::DetachFromSocketCommand command;
            command.childEntity = child;
            events::EventDispatcher::instance().execute(command);
        }

        void setActive(const services::EntityHandle& child, bool active)
        {
            events::socket::SetSocketActiveCommand command;
            command.entity = child;
            command.active = active;
            events::EventDispatcher::instance().execute(command);
        }

        void setTransform(const services::EntityHandle& child, const services::TransformData& transform)
        {
            events::scene::SetTransformCommand command;
            command.entity = child;
            command.transform = transform;
            events::EventDispatcher::instance().execute(command);
        }
    }

    // ------------------------------------------------------------------
    // SocketAttachmentUndo
    // ------------------------------------------------------------------

    SocketAttachmentUndo::SocketAttachmentUndo(uint32_t child, std::string description, SocketAttachmentState before,
                                               SocketAttachmentState after)
        : child(child), description(std::move(description)), before(std::move(before)), after(std::move(after))
    {
    }

    void SocketAttachmentUndo::apply(const SocketAttachmentState& state) const
    {
        using Kind = SocketAttachmentState::Kind;
        auto& dispatcher = events::EventDispatcher::instance();

        const services::EntityHandle handle = requireLive(child);
        const std::optional<events::socket::SocketAttachmentData> current = tools::rig::socketAttachment(handle);
        const bool linked = current.has_value() && !current->socketName.empty();

        switch (state.kind)
        {
        case Kind::None:
            if (current.has_value())
            {
                detach(handle);
            }
            break;

        case Kind::Empty:
            if (linked)
            {
                detach(handle);
            }
            if (!current.has_value() || linked)
            {
                events::socket::AddSocketAttachmentComponentCommand add;
                add.entity = handle;
                if (!dispatcher.execute(add))
                {
                    throw std::runtime_error("adding an empty SocketAttachment component to entity " + idText(child) +
                                             " failed");
                }
            }
            break;

        case Kind::Attached:
        {
            const services::EntityHandle parent = currentHandle(state.parent);
            if (!tools::rig::isLiveEntity(parent))
            {
                throw std::runtime_error("socket parent " + idText(state.parent) + " of entity " + idText(child) +
                                         " no longer exists");
            }
            // A link that already holds re-attaches for nothing (AttachToSocket would move the child).
            const bool sameLink = linked && current->parentEntity == parent && current->socketName == state.socket;
            if (!sameLink)
            {
                events::socket::AttachToSocketCommand attach;
                attach.childEntity = handle;
                attach.parentEntity = parent;
                attach.socketName = state.socket;
                if (!dispatcher.execute(attach))
                {
                    throw std::runtime_error("attaching entity " + idText(child) + " to socket '" + state.socket +
                                             "' of entity " + idText(state.parent) + " failed");
                }
            }
            setActive(handle, state.active);
            break;
        }
        }

        if (state.transform.has_value())
        {
            setTransform(handle, *state.transform);
        }
    }

    void SocketAttachmentUndo::execute() { apply(after); }
    void SocketAttachmentUndo::undo() { apply(before); }

    std::string SocketAttachmentUndo::getDescription() const
    {
        return description;
    }

    size_t SocketAttachmentUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + description.size() + before.socket.size() + after.socket.size();
    }

    // ------------------------------------------------------------------
    // SocketActiveUndo
    // ------------------------------------------------------------------

    SocketActiveUndo::SocketActiveUndo(uint32_t child, std::string childName, bool before, bool after,
                                       std::optional<services::TransformData> transformBefore)
        : child(child), childName(std::move(childName)), before(before), after(after),
          transformBefore(std::move(transformBefore))
    {
    }

    void SocketActiveUndo::apply(bool active) const
    {
        const services::EntityHandle handle = requireLive(child);
        const std::optional<events::socket::SocketAttachmentData> current = tools::rig::socketAttachment(handle);
        if (!current.has_value() || current->socketName.empty())
        {
            throw std::runtime_error("entity " + idText(child) + " is no longer attached to a socket");
        }
        setActive(handle, active);
    }

    void SocketActiveUndo::execute()
    {
        apply(after);
    }

    void SocketActiveUndo::undo()
    {
        apply(before);
        if (transformBefore.has_value())
        {
            setTransform(currentHandle(child), *transformBefore);
        }
    }

    std::string SocketActiveUndo::getDescription() const
    {
        return (after ? "MCP: Activate socket attachment (" : "MCP: Deactivate socket attachment (") + childName + ")";
    }

    size_t SocketActiveUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + childName.size();
    }
}
