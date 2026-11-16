#include "ProcessCodePage.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace launch
{
    unsigned int activeCodePage() noexcept
    {
        return GetACP();
    }

    bool processUsesUtf8() noexcept
    {
        return GetACP() == CP_UTF8;
    }

    Utf8ConsoleOutput::Utf8ConsoleOutput() noexcept
    {
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        // Not GetFileType == FILE_TYPE_CHAR: the NUL device is a character device too.
        if (output == nullptr || output == INVALID_HANDLE_VALUE || !GetConsoleMode(output, &mode))
        {
            return;
        }
        const UINT current = GetConsoleOutputCP();
        if (current == 0 || current == CP_UTF8)
        {
            return;
        }
        if (SetConsoleOutputCP(CP_UTF8))
        {
            previousCodePage = current;
        }
    }

    Utf8ConsoleOutput::~Utf8ConsoleOutput()
    {
        // Only while the console is still at UTF-8: something else may have changed it since.
        if (previousCodePage != 0 && GetConsoleOutputCP() == CP_UTF8)
        {
            SetConsoleOutputCP(previousCodePage);
        }
    }
}
