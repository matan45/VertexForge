#pragma once

namespace controllers::imguiHandler
{
    class ImguiWindow
    {
    public:
        virtual void draw() = 0;
        virtual bool shouldClose() const { return false; }
        virtual ~ImguiWindow() = default;
    };
};
