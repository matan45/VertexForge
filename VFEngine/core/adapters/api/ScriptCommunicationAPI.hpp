#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core
{
    class ScriptCommunicationManager;
}

namespace core::api
{
    class ScriptCommunicationAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
        static void setManager(ScriptCommunicationManager* manager);

    private:
        static inline ScriptCommunicationManager* communicationManager = nullptr;
    };
}
