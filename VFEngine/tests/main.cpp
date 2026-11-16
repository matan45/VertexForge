#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "launch/ProcessCodePage.hpp"

// Tests.exe embeds the UTF-8 code page manifest like Editor.exe and Runtime.exe (VK-1684), so the
// suite runs with the same narrow-string encoding as the engine (test_unicode_paths checks it).
int main(int argc, char** argv)
{
    // A console shows the UTF-8 output (assertion values included) as UTF-8. Destroyed last.
    launch::Utf8ConsoleOutput utf8Console;

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    context.setOption("no-breaks", true);

    int result = context.run();

    if (context.shouldExit())
        return result;

    return result;
}
