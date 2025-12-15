#include "../handlers/RuntimeHandler.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    handlers::RuntimeHandler runtime;

    try {
        runtime.init();

        // Load scene if provided as argument
        if (argc > 1) {
            if (!runtime.loadScene(argv[1])) {
                std::cerr << "Failed to load scene: " << argv[1] << std::endl;
            }
        }

        runtime.run();
        runtime.cleanUp();
    }
    catch (const std::exception& e) {
        std::cerr << "Runtime error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}



