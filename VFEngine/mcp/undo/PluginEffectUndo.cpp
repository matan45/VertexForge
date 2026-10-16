#include "PluginEffectUndo.hpp"

#include "../tools/RenderSettingsToolSupport.hpp"

#include <stdexcept>
#include <utility>

namespace mcp::undo
{
    PluginPostProcessEffectUndo::PluginPostProcessEffectUndo(uint64_t id, std::string label, bool before, bool after)
        : id(id), label(std::move(label)), before(before), after(after)
    {
    }

    void PluginPostProcessEffectUndo::apply(bool enabled) const
    {
        if (!tools::env::setPluginEffectEnabled(id, enabled))
        {
            throw std::runtime_error("the plugin post-process effect " + label + " (id " + std::to_string(id) +
                                     ") no longer exists");
        }
    }

    void PluginPostProcessEffectUndo::execute() { apply(after); }
    void PluginPostProcessEffectUndo::undo() { apply(before); }

    std::string PluginPostProcessEffectUndo::getDescription() const
    {
        return std::string(after ? "MCP: Enable " : "MCP: Disable ") + label;
    }

    size_t PluginPostProcessEffectUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + label.capacity();
    }
}
