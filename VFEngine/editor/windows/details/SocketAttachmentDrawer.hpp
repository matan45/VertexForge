#pragma once
#include "data/EntityHandle.hpp"

namespace windows::details
{
    class SocketAttachmentDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
    };
}
