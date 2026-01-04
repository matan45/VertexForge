#pragma once
#include "nfd/FileDialog.hpp"
#include <vector>
#include <string>

namespace windows
{
    class ImportModalDialog
    {
    private:
        nfd::FileDialog fileDialog;
        std::vector<std::string> files;
        std::vector<bool> isFlip;
        bool openModal = false;

    public:
        void draw();

        // Opens file selection dialog and queues modal
        void openImportDialog();

        bool isOpen() const { return openModal; }
    };
}
