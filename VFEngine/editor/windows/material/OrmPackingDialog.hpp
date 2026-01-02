#pragma once
#include <string>

namespace editor::materialeditor
{
    class OrmPackingDialog
    {
    private:
        bool showDialog = false;

        std::string aoPath;
        std::string roughnessPath;
        std::string metallicPath;
        std::string outputPath;

        std::string errorMessage;
        float progress = 0.0f;
        bool packInProgress = false;

    public:
        explicit OrmPackingDialog() = default;
        ~OrmPackingDialog() = default;

        void open();
        void draw();
        bool isOpen() const { return showDialog; }

    private:
        void resetState();
        void doPack();
    };
}
