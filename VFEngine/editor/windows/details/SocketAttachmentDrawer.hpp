#pragma once
#include "data/EntityHandle.hpp"
#include <string>
#include <vector>

namespace windows::details
{
    class SocketAttachmentDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);

        // Cached data for dropdowns
        std::vector<services::EntityHandle> candidateParents;
        std::vector<std::string> candidateNames;
        std::vector<std::string> socketNames;

        services::EntityHandle selectedParent;
        int selectedParentIdx = -1;
        int selectedSocketIdx = -1;
        bool needsRefresh = true;

        void refreshCandidateParents();
        void refreshSocketNames(services::EntityHandle parentEntity);
    };
}
