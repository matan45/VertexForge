#include "../handlers/RuntimeHandler.hpp"
#include <iostream>

int main(int argc, char* argv[])
{
    handlers::RuntimeHandler runtime;

    try
    {
        runtime.init();

        // Load project file
        if (argc > 1)
        {
            if (!runtime.loadProject(argv[1]))
            {
                std::cerr << "Failed to load project: " << argv[1] << std::endl;
            }
        }

        runtime.run();
        runtime.cleanUp();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Runtime error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
