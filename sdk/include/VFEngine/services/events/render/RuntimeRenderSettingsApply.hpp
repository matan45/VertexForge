#pragma once

#include "../EventDispatcher.hpp"
#include "RenderEvents.hpp"
#include "../project/ApplicationEvents.hpp"
#include "../vfx/VFXRuntimeEvents.hpp"
#include "../animation/AnimationBudgetEvents.hpp"
#include "types/RenderSettings.hpp"

namespace services::render
{
    // Single source of truth for the runtime-applicable subset of a RenderSettings: the shadow /
    // culling / distance / terrain / VT / dynamic-resolution bundle (ApplyShadowSettingsCommand),
    // the present-mode + MSAA swapchain rebuild (ApplyDisplaySettingsNotification), and the VFX /
    // animation LOD budgets. Deliberately EXCLUDES the scene-graph write and the post-process /
    // atmosphere / cloud commands — config is the runtime source of truth, and those would clobber
    // the scene-authored look. Called by GraphicsAPI (Graphics.applyPreset, script-side) and by
    // RuntimeHandler (the boot-time persisted graphics override), so both stay in lockstep.
    inline void applyRuntimeRenderSettings(::events::EventDispatcher& dispatcher,
                                           const types::RenderSettings& s)
    {
        ::events::render::ApplyShadowSettingsCommand shadowCmd;
        shadowCmd.settings = s;
        dispatcher.execute(shadowCmd);

        ::events::application::ApplyDisplaySettingsNotification displayNotif;
        displayNotif.presentMode = s.display.presentMode;
        displayNotif.msaa = s.display.msaa;
        dispatcher.publish(displayNotif);

        ::services::events::vfxruntime::SetVFXLODConfigCommand vfxCmd;
        vfxCmd.lod0Distance = s.vfxLOD.lod0Distance;
        vfxCmd.lod1Distance = s.vfxLOD.lod1Distance;
        vfxCmd.lod2Distance = s.vfxLOD.lod2Distance;
        vfxCmd.transitionZone = s.vfxLOD.transitionZone;
        dispatcher.execute(vfxCmd);

        ::services::events::animation::SetAnimationLODConfigCommand animCmd;
        animCmd.lod0Distance = s.animationLOD.lod0Distance;
        animCmd.lod1Distance = s.animationLOD.lod1Distance;
        animCmd.lod2Distance = s.animationLOD.lod2Distance;
        animCmd.lod3Distance = s.animationLOD.lod3Distance;
        animCmd.lod0Interval = s.animationLOD.lod0Interval;
        animCmd.lod1Interval = s.animationLOD.lod1Interval;
        animCmd.lod2Interval = s.animationLOD.lod2Interval;
        animCmd.maxStreamingInitPerFrame = s.animationLOD.maxStreamingInitPerFrame;
        dispatcher.execute(animCmd);
    }
}
