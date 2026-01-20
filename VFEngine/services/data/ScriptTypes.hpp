#pragma once
#include "EntityHandle.hpp"
#include <string>

namespace services {

    // Playback state for script execution control
    enum class ScriptPlaybackState {
        Stopped,    // Not started or explicitly stopped
        Playing,    // Running onUpdate every frame
        Paused      // Started but onUpdate skipped
    };

    // Parameters controlling script playback behavior
    struct ScriptPlaybackParams {
        bool loop = false;           // Restart onStart if stopped/error
        float playbackSpeed = 1.0f;  // Multiplier for deltaTime passed to onUpdate
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
