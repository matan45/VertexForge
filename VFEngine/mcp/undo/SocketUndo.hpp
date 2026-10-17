#pragma once

#include "data/DTOs.hpp"
#include "data/UndoTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// VK-1656: undo entries for the socket tools (tools/AnimationSocketTools.cpp). They store the entity ids
// seen when the edit was recorded, resolve them through EntityIdRemap, and replay the socket and transform
// commands the tools use. Failures throw; the McpBatchUndoCommand the tools push logs them.
namespace mcp::undo
{
    // A child's socket attachment as far as the engine API can restore it.
    struct SocketAttachmentState
    {
        enum class Kind
        {
            None,     // no SocketAttachment component
            Empty,    // a component that names no socket
            Attached  // attached to `socket` of `parent`
        };

        Kind kind = Kind::None;
        uint32_t parent = 0;  // Attached: the recorded parent id
        std::string socket;   // Attached
        bool active = true;   // Attached
        // The child's local transform, restored last: AttachToSocket overwrites the position and
        // DetachFromSocket leaves the last socket WORLD position in it.
        std::optional<services::TransformData> transform;
    };

    // animation_attach_to_socket / animation_detach_from_socket. Replaying a state: None ->
    // DetachFromSocket; Empty -> DetachFromSocket + AddSocketAttachmentComponent (nothing when the
    // component is already empty); Attached -> AttachToSocket (skipped when already linked to that socket
    // of that parent) + SetSocketActive; then SetTransform with the state's transform.
    class SocketAttachmentUndo : public services::IUndoableCommand
    {
    public:
        SocketAttachmentUndo(uint32_t child, std::string description, SocketAttachmentState before,
                             SocketAttachmentState after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(const SocketAttachmentState& state) const;

        uint32_t child;
        std::string description;
        SocketAttachmentState before;
        SocketAttachmentState after;
    };

    // animation_set_socket_active. Undo also restores the child's local transform from before the
    // switch: an attachment switched on moves the child, and switching it off again would leave it there.
    class SocketActiveUndo : public services::IUndoableCommand
    {
    public:
        SocketActiveUndo(uint32_t child, std::string childName, bool before, bool after,
                         std::optional<services::TransformData> transformBefore);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(bool active) const;

        uint32_t child;
        std::string childName;
        bool before;
        bool after;
        std::optional<services::TransformData> transformBefore;
    };
}
