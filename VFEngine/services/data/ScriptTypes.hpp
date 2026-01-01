#pragma once
#include "EntityHandle.hpp"
#include <string>
#include <vector>
#include <optional>

namespace services {

    // Data for attaching a script to an entity
    struct ScriptData {
        std::string scriptPath;     // Path to .mt source file
        bool enabled = true;
    };

    // Information about a script's public properties
    struct ScriptPropertyInfo {
        std::string name;
        std::string typeName;       // "float", "int", "string", "Vec3f", etc.
        bool isReadOnly = false;
    };

    // Information about a script's callable methods
    struct ScriptMethodInfo {
        std::string name;
        std::vector<std::string> parameterTypes;
        std::string returnType;
    };

    // Information about a loaded script instance
    struct ScriptInstanceInfo {
        uint64_t instanceId = 0;
        std::string className;
        std::string scriptPath;
        bool hasOnStart = false;
        bool hasOnUpdate = false;
        bool hasOnDestroy = false;
    };

    // Error information from script compilation or execution
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

    // Component data for serialization
    struct ScriptComponentData {
        std::string scriptPath;
        bool enabled = true;
    };

}
