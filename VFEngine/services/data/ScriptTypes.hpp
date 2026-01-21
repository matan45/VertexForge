#pragma once
#include "EntityHandle.hpp"
#include <string>

namespace services {

    enum class ScriptPlaybackState {
        Stopped,
        Playing
    };

    // Data for attaching a script to an entity
    struct ScriptData {
        std::string scriptPath;     // Path to .mt source file
        bool enabled = true;
    };

    // Information about a loaded script instance
    struct ScriptInstanceInfo {
        uint64_t instanceId = 0;
        std::string className;
        std::string scriptPath;
    };
    
    struct ScriptError {
        enum class Type {
            Compile,    // Syntax or type error during compilation
            Runtime     // Error during script execution
        };

        Type type = Type::Compile;
        std::string message;
        std::string file;
        int line = 0;
        int column = 0;
    };

}
