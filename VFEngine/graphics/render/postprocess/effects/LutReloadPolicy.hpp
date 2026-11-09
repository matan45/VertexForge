#pragma once
#include <string>

namespace render::postprocess
{
    // One LUT slot's deferred reload (ColorGradingEffect loads `pending` in preRecord). Compares the request
    // against the path that WILL be loaded (the pending one while a reload is queued), so a second change
    // queued before preRecord runs - e.g. an MCP edit and its undo in one main-thread drain - is not dropped;
    // asking for the already-loaded path cancels a queued reload.
    inline void requestLut(const std::string& current, std::string& pending, bool& pendingActive,
                           const std::string& requested)
    {
        const std::string& target = pendingActive ? pending : current;
        if (requested == target)
            return;
        if (requested == current)
        {
            pendingActive = false;
            pending.clear();
            return;
        }
        pending = requested;
        pendingActive = true;
    }
}
