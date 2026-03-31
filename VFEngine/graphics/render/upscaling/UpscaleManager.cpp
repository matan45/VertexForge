#include "UpscaleManager.hpp"
#include "../../core/Device.hpp"
#include "../../../utilities/print/Log.hpp"

#ifdef VF_STREAMLINE_ENABLED
#include <sl.h>
#include <sl_dlss.h>
#include <sl_consts.h>
#include <sl_helpers_vk.h>
#endif

namespace render::upscaling
{
    UpscaleManager::~UpscaleManager()
    {
        shutdown();
    }

    bool UpscaleManager::initStreamline()
    {
#ifdef VF_STREAMLINE_ENABLED
        if (streamlineInitialized) return streamlineAvailable;

        sl::Preferences prefs{};
        prefs.showConsole = false;
        prefs.logLevel = sl::LogLevel::eOff;
        prefs.featuresToLoad = nullptr;
        prefs.numFeaturesToLoad = 0; // Load all available features

        // Use manual hooking mode (not interposer proxy)
        prefs.flags = sl::PreferenceFlags::eUseManualHooking;

        sl::Result result = slInit(prefs);
        streamlineInitialized = true;

        if (result == sl::Result::eOk)
        {
            streamlineAvailable = true;
            vfLogInfo("Streamline SDK initialized successfully");
        }
        else
        {
            streamlineAvailable = false;
            vfLogWarning("Streamline SDK init failed (result={}). "
                         "Upscaling features will not be available.",
                         static_cast<int>(result));
        }

        return streamlineAvailable;
#else
        vfLogInfo("Streamline SDK not enabled (VF_STREAMLINE_ENABLED not defined)");
        return false;
#endif
    }

    bool UpscaleManager::setVulkanDevice(core::Device& device)
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!streamlineAvailable) return false;

        sl::VulkanInfo vkInfo{};
        vkInfo.device = static_cast<VkDevice>(device.getLogicalDevice());
        vkInfo.instance = static_cast<VkInstance>(device.getInstance());
        vkInfo.physicalDevice = static_cast<VkPhysicalDevice>(device.getPhysicalDevice());
        vkInfo.graphicsQueueFamily = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        vkInfo.graphicsQueueIndex = 0;
        vkInfo.computeQueueFamily = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        vkInfo.computeQueueIndex = 0;

        sl::Result result = slSetVulkanInfo(vkInfo);
        if (result != sl::Result::eOk)
        {
            vfLogWarning("Streamline slSetVulkanInfo failed (result={})", static_cast<int>(result));
            return false;
        }

        deviceSet = true;
        queryFeatureSupport();

        vfLogInfo("Streamline Vulkan device set. DLSS={}, DirectSR={}",
                  dlssSupported ? "supported" : "not supported",
                  directSRSupported ? "supported" : "not supported");
        return true;
#else
        return false;
#endif
    }

    void UpscaleManager::shutdown()
    {
#ifdef VF_STREAMLINE_ENABLED
        if (streamlineInitialized && streamlineAvailable)
        {
            slShutdown();
            streamlineAvailable = false;
            streamlineInitialized = false;
            deviceSet = false;
            vfLogInfo("Streamline SDK shut down");
        }
#endif
    }

    void UpscaleManager::queryFeatureSupport()
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!deviceSet) return;

        sl::AdapterInfo adapterInfo{};
        dlssSupported = (slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo) == sl::Result::eOk);
        directSRSupported = (slIsFeatureSupported(sl::kFeatureDirectSR, adapterInfo) == sl::Result::eOk);
#endif
    }

    postprocess::UpscaleMode UpscaleManager::resolveActiveMode(postprocess::UpscaleMode requested) const
    {
        if (requested == postprocess::UpscaleMode::Off)
            return postprocess::UpscaleMode::Off;

        if (requested == postprocess::UpscaleMode::DLSS)
            return dlssSupported ? postprocess::UpscaleMode::DLSS : postprocess::UpscaleMode::Off;

        if (requested == postprocess::UpscaleMode::FSR2)
            return directSRSupported ? postprocess::UpscaleMode::FSR2 : postprocess::UpscaleMode::Off;

        // Auto: prefer DLSS, fall back to DirectSR
        if (requested == postprocess::UpscaleMode::Auto)
        {
            if (dlssSupported) return postprocess::UpscaleMode::DLSS;
            if (directSRSupported) return postprocess::UpscaleMode::FSR2;
            return postprocess::UpscaleMode::Off;
        }

        return postprocess::UpscaleMode::Off;
    }

    void UpscaleManager::applySettings(const postprocess::UpscaleSettings& settings,
                                        uint32_t outputWidth, uint32_t outputHeight)
    {
        activeMode = settings.enabled
            ? resolveActiveMode(settings.mode)
            : postprocess::UpscaleMode::Off;

        resolutionManager.setDisplayResolution(outputWidth, outputHeight);

        if (activeMode != postprocess::UpscaleMode::Off)
            resolutionManager.setQualityMode(settings.quality);
        else
            resolutionManager.setQualityMode(postprocess::UpscaleQuality::Native);

#ifdef VF_STREAMLINE_ENABLED
        if (!deviceSet || activeMode == postprocess::UpscaleMode::Off) return;

        if (activeMode == postprocess::UpscaleMode::DLSS)
        {
            sl::DLSSOptions dlssOptions{};

            switch (settings.quality)
            {
            case postprocess::UpscaleQuality::Native:          dlssOptions.mode = sl::DLSSMode::eDLAA; break;
            case postprocess::UpscaleQuality::Quality:         dlssOptions.mode = sl::DLSSMode::eMaxQuality; break;
            case postprocess::UpscaleQuality::Balanced:        dlssOptions.mode = sl::DLSSMode::eBalanced; break;
            case postprocess::UpscaleQuality::Performance:     dlssOptions.mode = sl::DLSSMode::eMaxPerformance; break;
            case postprocess::UpscaleQuality::UltraPerformance:dlssOptions.mode = sl::DLSSMode::eUltraPerformance; break;
            }

            dlssOptions.outputWidth = outputWidth;
            dlssOptions.outputHeight = outputHeight;
            dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;

            slDLSSSetOptions(sl::ViewportHandle{0}, dlssOptions);
        }
#endif
    }

    void UpscaleManager::evaluate(vk::CommandBuffer cmd, uint32_t frameIndex,
                                   const UpscaleInputs& inputs)
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!deviceSet || activeMode == postprocess::UpscaleMode::Off) return;

        sl::Feature feature = (activeMode == postprocess::UpscaleMode::DLSS)
            ? sl::kFeatureDLSS
            : sl::kFeatureDirectSR;

        sl::ViewportHandle viewport{0};

        // Get frame token
        sl::FrameToken* frameToken = nullptr;
        slGetNewFrameToken(frameToken, &frameIndex);
        if (!frameToken) return;

        // Set constants (camera jitter, motion vector info, etc.)
        sl::Constants constants{};
        constants.jitterOffset = {inputs.jitterOffset.x, inputs.jitterOffset.y};
        constants.mvecScale = {1.0f, 1.0f}; // Motion vectors in pixel space
        constants.reset = inputs.resetAccumulation ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        constants.depthInverted = sl::Boolean::eTrue; // Reverse-Z
        constants.cameraPinholeOffset = {0.0f, 0.0f};
        slSetConstants(constants, *frameToken, viewport);

        // Tag resources
        sl::ResourceTag tags[5]{};
        uint32_t tagCount = 0;

        sl::Resource colorRes{sl::ResourceType::eTex2d, inputs.colorInput,
                              nullptr, static_cast<VkImageView>(inputs.colorView),
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        tags[tagCount++] = sl::ResourceTag{&colorRes, sl::kBufferTypeScalingInputColor,
                                            sl::ResourceLifecycle::eOnlyValidNow, nullptr};

        sl::Resource depthRes{sl::ResourceType::eTex2d, inputs.depthInput,
                              nullptr, static_cast<VkImageView>(inputs.depthView),
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        tags[tagCount++] = sl::ResourceTag{&depthRes, sl::kBufferTypeDepth,
                                            sl::ResourceLifecycle::eOnlyValidNow, nullptr};

        sl::Resource mvecRes{sl::ResourceType::eTex2d, inputs.motionVectors,
                             nullptr, static_cast<VkImageView>(inputs.motionView),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        tags[tagCount++] = sl::ResourceTag{&mvecRes, sl::kBufferTypeMotionVectors,
                                            sl::ResourceLifecycle::eOnlyValidNow, nullptr};

        sl::Resource outputRes{sl::ResourceType::eTex2d, inputs.output,
                               nullptr, static_cast<VkImageView>(inputs.outputView),
                               VK_IMAGE_LAYOUT_GENERAL};
        tags[tagCount++] = sl::ResourceTag{&outputRes, sl::kBufferTypeScalingOutputColor,
                                            sl::ResourceLifecycle::eOnlyValidNow, nullptr};

        if (inputs.reactiveMask)
        {
            sl::Resource reactiveRes{sl::ResourceType::eTex2d, inputs.reactiveMask,
                                     nullptr, static_cast<VkImageView>(inputs.reactiveView),
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            tags[tagCount++] = sl::ResourceTag{&reactiveRes, sl::kBufferTypeTransparencyHint,
                                                sl::ResourceLifecycle::eOnlyValidNow, nullptr};
        }

        slSetTagForFrame(*frameToken, viewport, tags, tagCount,
                         reinterpret_cast<sl::CommandBuffer*>(static_cast<VkCommandBuffer>(cmd)));

        // Evaluate
        const sl::BaseStructure* evalInputs[] = {nullptr};
        slEvaluateFeature(feature, *frameToken, evalInputs, 0,
                          reinterpret_cast<sl::CommandBuffer*>(static_cast<VkCommandBuffer>(cmd)));
#endif
    }

    void UpscaleManager::resetHistory()
    {
        // The next evaluate() call will pass resetAccumulation = true
        // which is handled via sl::Constants::reset in evaluate()
    }
}
