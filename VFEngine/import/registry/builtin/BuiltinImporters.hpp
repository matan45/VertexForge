#pragma once
#include "../../ImportExport.hpp"

namespace import::builtin
{
    // Registers the engine's built-in importers into ImporterRegistry::instance()
    // exactly once (owner tag "engine"). Safe to call from any entry point that
    // needs the registry populated (pipeline stages, FileUtils, editor queries).
    VF_IMPORT_API void ensureRegistered();
}
