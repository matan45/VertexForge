#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class TerrainAPI
    {
    private:
        // VK-1624. The radius is the load-bearing guard, not the call count: BrushSampler
        // enumerates every tile in the brush's AABB, so one call with a 10,000-unit radius costs
        // far more than 64 craters. The per-frame cap exists for the other shape of mistake -- a
        // script deforming from onUpdate without meaning to.
        static constexpr int MAX_EDITS_PER_FRAME = 64;
        static constexpr float MAX_BRUSH_RADIUS = 512.0f;
        inline static int editCountThisFrame = 0;

    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);

        // Resets the per-frame edit budget. Driven from NativeAPIRegistry::beginFrame, which both
        // EditorBootstrapFrame and RuntimeBootstrapFrame already call.
        static void beginFrame();

    private:
        // Shared by every mutating native: budget check + one warning per exhausted frame. Returns
        // false when the caller should return the "nothing happened" sentinel.
        static bool consumeEditBudget(const char* nativeName);
    };
}
