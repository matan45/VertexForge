#include "WaterUndo.hpp"
#include "EntityIdRemap.hpp"
#include "McpUndoCommands.hpp"
#include "../tools/ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/terrain/OceanEvents.hpp"

#include <optional>
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
    }

    // ------------------------------------------------------------------
    // OceanStateUndo
    // ------------------------------------------------------------------

    OceanStateUndo::OceanStateUndo(uint32_t ocean, services::OceanStateData before, services::OceanStateData after,
                                   tools::watertool::OceanParts parts, std::string description)
        : ocean(ocean), before(before), after(after), parts(parts), description(std::move(description))
    {
    }

    void OceanStateUndo::apply(const services::OceanStateData& source) const
    {
        const services::EntityHandle handle = currentHandle(ocean);
        std::optional<services::OceanStateData> current = tools::watertool::readOceanState(handle);
        if (!current.has_value())
        {
            throw std::runtime_error("entity " + idText(ocean) + " is no longer the scene's ocean");
        }
        tools::watertool::mergeParts(*current, source, parts);
        tools::watertool::writeOceanState(handle, *current);
    }

    void OceanStateUndo::execute() { apply(after); }
    void OceanStateUndo::undo() { apply(before); }

    std::string OceanStateUndo::getDescription() const
    {
        return description;
    }

    size_t OceanStateUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + description.size();
    }

    // ------------------------------------------------------------------
    // OceanLifetimeUndo
    // ------------------------------------------------------------------

    std::unique_ptr<OceanLifetimeUndo> OceanLifetimeUndo::capture(Kind kind, const services::EntityHandle& ocean)
    {
        const std::optional<services::EntityData> data = tools::findEntity(ocean);
        if (!data.has_value())
        {
            throw std::runtime_error("entity " + std::to_string(tools::toId(ocean)) + " does not exist");
        }
        if (!data->parent.has_value() || !data->parent->isValid())
        {
            throw std::runtime_error("the scene root cannot be recorded for undo");
        }
        const std::optional<services::OceanStateData> state = tools::watertool::readOceanState(ocean);
        if (!state.has_value())
        {
            throw std::runtime_error("entity " + std::to_string(tools::toId(ocean)) + " ('" + data->name +
                                     "') is not the scene's ocean");
        }

        std::unique_ptr<OceanLifetimeUndo> command(new OceanLifetimeUndo());
        command->kind = kind;
        command->id = tools::toId(ocean);
        command->name = data->name;
        command->parent = tools::toId(*data->parent);
        command->siblingIndex = tools::childIndex(*data->parent, ocean).value_or(-1);
        command->state = *state;
        return command;
    }

    void OceanLifetimeUndo::instantiate() const
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // One ocean per scene: OceanService refuses a second one, and would lose track of it if it did not.
        const services::EntityHandle existing = tools::watertool::trackedOcean();
        if (existing.isValid() && tools::watertool::readOceanState(existing).has_value())
        {
            throw std::runtime_error("re-creating the ocean '" + name + "' was refused: the scene already has an ocean "
                                     "(entity " + std::to_string(tools::toId(existing)) + ")");
        }

        events::ocean::CreateOceanCommand create;
        create.config = tools::watertool::creationFromState(state);
        const services::EntityHandle created = dispatcher.execute(create);
        if (!created.isValid())
        {
            throw std::runtime_error("re-creating the ocean '" + name + "' failed");
        }

        try
        {
            tools::watertool::writeOceanState(created, state);
        }
        catch (...)
        {
            // Never leave a half-restored ocean (default look, visible) behind.
            events::scene::DeleteEntityCommand remove;
            remove.entity = created;
            dispatcher.execute(remove);
            throw;
        }

        // CreateOcean names it "Ocean" and appends it under the scene root.
        const std::optional<services::EntityData> data = tools::findEntity(created);
        if (data.has_value() && data->name != name)
        {
            events::scene::SetEntityNameCommand rename;
            rename.entity = created;
            rename.newName = name;
            dispatcher.execute(rename);
        }
        const services::EntityHandle parentHandle = currentHandle(parent);
        if (tools::findEntity(parentHandle).has_value())
        {
            const std::optional<int> currentIndex = tools::childIndex(parentHandle, created);
            if (!currentIndex.has_value() || (siblingIndex >= 0 && *currentIndex != siblingIndex))
            {
                events::scene::ReorderEntityCommand reorder;
                reorder.entity = created;
                reorder.newParent = parentHandle;
                reorder.insertIndex = siblingIndex >= 0 ? reorderInsertIndex(currentIndex, siblingIndex) : -1;
                dispatcher.execute(reorder);
            }
        }

        EntityIdRemap::instance().record(id, tools::toId(created));
    }

    void OceanLifetimeUndo::destroy() const
    {
        auto& dispatcher = events::EventDispatcher::instance();
        const services::EntityHandle handle = currentHandle(id);

        events::ocean::HasOceanComponentQuery isOcean;
        isOcean.entity = handle;
        if (!dispatcher.query(isOcean))
        {
            throw std::runtime_error("entity " + idText(id) + " ('" + name + "') is no longer an ocean");
        }

        // Re-creating the ocean (the other half of this entry) brings back the ocean only, so deleting what was
        // added to it since - children, other components - could never be undone. ocean_enable {remove:true}
        // refuses for the same reason.
        const tools::watertool::OceanExtras extras = tools::watertool::oceanExtras(handle);
        if (!extras.empty())
        {
            throw std::runtime_error("the ocean '" + name + "' (" + idText(id) + ") now has " +
                                     std::to_string(extras.children) + " child entities and " +
                                     std::to_string(extras.components.size()) + " other components, which deleting it "
                                     "would lose for good; it was left in place");
        }

        events::scene::DeleteEntityCommand command;
        command.entity = handle;
        if (!dispatcher.execute(command))
        {
            throw std::runtime_error("deleting the ocean '" + name + "' (" + idText(id) + ") failed");
        }
    }

    void OceanLifetimeUndo::execute()
    {
        kind == Kind::Created ? instantiate() : destroy();
    }

    void OceanLifetimeUndo::undo()
    {
        kind == Kind::Created ? destroy() : instantiate();
    }

    std::string OceanLifetimeUndo::getDescription() const
    {
        return std::string("MCP: ") + (kind == Kind::Created ? "Create" : "Remove") + " ocean (" + name + ")";
    }

    size_t OceanLifetimeUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + name.size();
    }

    // ------------------------------------------------------------------
    // WaterBodyUndo
    // ------------------------------------------------------------------

    WaterBodyUndo::WaterBodyUndo(uint32_t entity, std::string entityName, services::WaterBodyComponentData before,
                                 services::WaterBodyComponentData after)
        : entity(entity), entityName(std::move(entityName)), before(before), after(after)
    {
    }

    void WaterBodyUndo::apply(const services::WaterBodyComponentData& data) const
    {
        auto& dispatcher = events::EventDispatcher::instance();
        const services::EntityHandle handle = currentHandle(entity);

        events::ocean::HasWaterBodyComponentQuery isBody;
        isBody.entity = handle;
        if (!dispatcher.query(isBody))
        {
            throw std::runtime_error("entity " + idText(entity) + " ('" + entityName + "') has no water body any more");
        }

        events::ocean::SetWaterBodyDataCommand command;
        command.entity = handle;
        command.data = data;
        dispatcher.execute(command);
    }

    void WaterBodyUndo::execute() { apply(after); }
    void WaterBodyUndo::undo() { apply(before); }

    std::string WaterBodyUndo::getDescription() const
    {
        return "MCP: Set water body (" + entityName + ")";
    }

    size_t WaterBodyUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + entityName.size();
    }
}
