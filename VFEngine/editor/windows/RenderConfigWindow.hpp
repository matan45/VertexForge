#pragma once

namespace windows
{
    class RenderConfigWindow
    {
    private:
        bool visible = false;

    public:
        void draw();
        void show();
    };
}
