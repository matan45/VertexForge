#pragma once
#include "EntityHandle.hpp"
#include <string>

namespace services {

    enum class ScriptPlaybackState {
        Stopped,
        Playing
    };

    // Data for attaching a script to an entity. Mirrors the authored (serialized) half of
    // components::ScriptEntry — anything added here must also be carried by the duplicate path
    // (HierarchyService::duplicateRecursive), which reattaches scripts through AttachScriptCommand
    // rather than value-copying the component.
    struct ScriptData {
        std::string scriptPath;     // Path to .mt source file
        bool enabled = true;
        int inputPriority = 0;      // execution/input order (higher first)

        // VK-1536 tick governor. 0 == every frame == disabled.
        float updateInterval = 0.0f;
        float tickSignificance = 1.0f;
        bool pinFullRate = false;
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
