#include "IkStateUndo.hpp"
#include "McpUndoCommands.hpp"
#include "../tools/ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/physics/IKEvents.hpp"

#include <stdexcept>
#include <utility>

namespace mcp::undo
{
    namespace
    {
        size_t chainBytes(const std::vector<animator::ik::IKChainConfig>& chains)
        {
            size_t total = 0;
            for (const animator::ik::IKChainConfig& chain : chains)
            {
                total += sizeof(chain) + chain.chainName.size() + chain.tipBoneName.size() +
                         chain.constraints.size() * sizeof(animator::ik::JointConstraint);
                for (const std::string& bone : chain.chainBoneNames)
                {
                    total += sizeof(bone) + bone.size();
                }
            }
            return total;
        }
    }

    IkStateUndo::IkStateUndo(uint32_t entity, std::string entityName, IkState before, IkState after)
        : entity(entity), entityName(std::move(entityName)), before(std::move(before)), after(std::move(after))
    {
        footprint = sizeof(*this) + this->entityName.size() + chainBytes(this->before.chains) +
                    chainBytes(this->after.chains);
    }

    void IkStateUndo::apply(const IkState& state) const
    {
        const services::EntityHandle handle = currentHandle(entity);
        if (!tools::findEntity(handle).has_value())
        {
            throw std::runtime_error("entity " + std::to_string(entity) + " no longer exists");
        }

        auto& dispatcher = events::EventDispatcher::instance();
        events::ik::HasIKComponentQuery hasQuery;
        hasQuery.entity = handle;
        const bool present = dispatcher.query(hasQuery);

        if (!state.hasComponent)
        {
            if (present)
            {
                events::ik::RemoveIKComponentCommand remove;
                remove.entity = handle;
                if (!dispatcher.execute(remove))
                {
                    throw std::runtime_error("removing the IK component from entity " + std::to_string(entity) +
                                             " failed");
                }
            }
            return;
        }

        if (!present)
        {
            events::ik::AddIKComponentCommand add;
            add.entity = handle;
            if (!dispatcher.execute(add))
            {
                throw std::runtime_error("adding the IK component to entity " + std::to_string(entity) + " failed");
            }
        }

        events::ik::SetIKChainsCommand set;
        set.entity = handle;
        set.chains = state.chains;
        if (!dispatcher.execute(set))
        {
            throw std::runtime_error("setting the IK chains of entity " + std::to_string(entity) + " failed");
        }
    }

    void IkStateUndo::execute() { apply(after); }
    void IkStateUndo::undo() { apply(before); }

    std::string IkStateUndo::getDescription() const
    {
        return (after.hasComponent ? "MCP: Set IK chains (" : "MCP: Remove IK component (") + entityName + ")";
    }

    size_t IkStateUndo::getMemoryFootprint() const
    {
        return footprint;
    }
}
