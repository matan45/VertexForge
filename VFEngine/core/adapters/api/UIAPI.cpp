// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "UIAPI.hpp"
#include "UIButtonLabelAPI.hpp"
#include "UIInputAPI.hpp"
#include "UIValueAPI.hpp"
#include "print/EditorLogger.hpp"

namespace core::api
{
    void UIAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        UIButtonLabelAPI::registerAPI(interpreter);
        UIInputAPI::registerAPI(interpreter);
        UIValueAPI::registerAPI(interpreter);

        vfLogInfo("[UIAPI] Registered UI native functions");
    }
}
