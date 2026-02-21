#pragma once
#include "data/EntityHandle.hpp"
#include <string>
#include <vector>

namespace events::socket { struct SocketAttachmentData; }

namespace windows::details
{
    class SocketAttachmentDrawer
    {
    private:
        // Cached data for dropdowns
        std::vector<services::EntityHandle> candidateParents;
        std::vector<std::string> candidateNames;
        std::vector<std::string> socketNames;

        services::EntityHandle selectedParent;
        int selectedParentIdx = -1;
        int selectedSocketIdx = -1;
        bool needsRefresh = true;

    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        void drawAttachedState(services::EntityHandle handle, const events::socket::SocketAttachmentData& data);
        void drawUnattachedState(services::EntityHandle handle);

        void refreshCandidateParents();
        void refreshSocketNames(services::EntityHandle parentEntity);
    };
}
