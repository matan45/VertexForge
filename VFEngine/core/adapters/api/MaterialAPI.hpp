#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    // Material:: natives — per-entity runtime overrides of named material parameters
    // (MaterialComponent::parameterOverrides). Values flow to the GPU-driven renderer
    // through PBR extraction; parameters must fold into PBR outputs to be visible in
    // the world view (see MaterialParameterSet / isParameterWorldVisible).
    class MaterialAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
