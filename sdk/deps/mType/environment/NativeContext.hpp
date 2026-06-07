#pragma once
#include <span>
#include <memory>
#include <string>

namespace environment {
    class Environment;
}

namespace vm::runtime {
    class VirtualMachine;
}

namespace services {
    class ScriptAPI;
}

namespace value {
    class Value;
}

namespace environment {
    /**
     * Context passed to native (FFI) functions.
     * Provides access to the environment and VM state.
     */
    struct NativeContext {
        std::shared_ptr<Environment> env;
        std::shared_ptr<vm::runtime::VirtualMachine> vm;
        services::ScriptAPI* api = nullptr;
        
        // Convenience for throwing mType exceptions from C++
        [[noreturn]] void throwException(const std::string& type, const std::string& message);
    };
}
