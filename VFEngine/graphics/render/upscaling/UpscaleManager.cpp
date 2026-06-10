#include "UpscaleManager.hpp"
#include "../../core/Device.hpp"
#include "../../../utilities/print/Log.hpp"

#ifdef VF_STREAMLINE_ENABLED
#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_consts.h>
#include <sl_helpers_vk.h>
#include <sl_matrix_helpers.h>
#endif

namespace render::upscaling
{
#ifdef VF_STREAMLINE_ENABLED
    // GLM is column-major, Streamline is row-major — transpose during conversion
    static sl::float4x4 toSL(const glm::mat4& m)
    {
        sl::float4x4 r;
        r[0] = {m[0][0], m[1][0], m[2][0], m[3][0]};
        r[1] = {m[0][1], m[1][1], m[2][1], m[3][1]};
        r[2] = {m[0][2], m[1][2], m[2][2], m[3][2]};
        r[3] = {m[0][3], m[1][3], m[2][3], m[3][3]};
        return r;
    }

    static const char* slResultToString(sl::Result r)
    {
        switch (r)
        {
        case sl::Result::eOk: return "Ok";
        case sl::Result::eErrorDriverOutOfDate: return "ErrorDriverOutOfDate";
        case sl::Result::eErrorOSOutOfDate: return "ErrorOSOutOfDate";
        case sl::Result::eErrorDeviceNotCreated: return "ErrorDeviceNotCreated";
        case sl::Result::eErrorNoSupportedAdapterFound: return "ErrorNoSupportedAdapterFound";
        case sl::Result::eErrorAdapterNotSupported: return "ErrorAdapterNotSupported";
        case sl::Result::eErrorNoPlugins: return "ErrorNoPlugins";
        case sl::Result::eErrorVulkanAPI: return "ErrorVulkanAPI";
        case sl::Result::eErrorNGXFailed: return "ErrorNGXFailed";
        case sl::Result::eErrorInvalidIntegration: return "ErrorInvalidIntegration";
        case sl::Result::eErrorNotInitialized: return "ErrorNotInitialized";
        case sl::Result::eErrorInitNotCalled: return "ErrorInitNotCalled";
        case sl::Result::eErrorFeatureMissing: return "ErrorFeatureMissing";
        case sl::Result::eErrorFeatureNotSupported: return "ErrorFeatureNotSupported";
        case sl::Result::eErrorFeatureFailedToLoad: return "ErrorFeatureFailedToLoad";
        case sl::Result::eErrorFeatureMissingDependency: return "ErrorFeatureMissingDependency";
        default: return "Unknown";
        }
    }
#endif

    UpscaleManager::~UpscaleManager()
    {
        shutdown();
    }

    bool UpscaleManager::initStreamline()
    {
#ifdef VF_STREAMLINE_ENABLED
        if (streamlineInitialized) return streamlineAvailable;

        sl::Feature featuresToLoad[] = {
            sl::kFeatureDLSS,
            sl::kFeatureDLSS_G,
            sl::kFeatureReflex,
            // PCL provides slPCLSetMarker; without it every latency marker fails to resolve.
            sl::kFeaturePCL
        };

        sl::Preferences prefs{};
        prefs.showConsole = false;
        prefs.logLevel = sl::LogLevel::eOff;
        prefs.featuresToLoad = featuresToLoad;
        prefs.numFeaturesToLoad = static_cast<uint32_t>(std::size(featuresToLoad));
        prefs.engine = sl::EngineType::eCustom;
        prefs.engineVersion = "1.0.0";
        prefs.renderAPI = sl::RenderAPI::eVulkan;
        // Vulkan calls are routed through sl.interposer.dll's vkGetInstanceProcAddr
        // (set up in Device::createInstance), so Streamline automatically tracks
        // vkCreateInstance/vkCreateDevice and all resource creation.
        // eUseFrameBasedResourceTagging is required for slSetTagForFrame().
        prefs.flags = sl::PreferenceFlags::eUseFrameBasedResourceTagging;

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
            vfLogWarning("Streamline SDK init failed: {} ({}). "
                         "Upscaling features will not be available.",
                         slResultToString(result), static_cast<int>(result));
        }

        return streamlineAvailable;
#else
        vfLogInfo("Streamline SDK not enabled (VF_STREAMLINE_ENABLED not defined)");
        return false;
#endif
    }

    std::vector<const char*> UpscaleManager::getRequiredInstanceExtensions()
    {
        std::vector<const char*> extensions;
#ifdef VF_STREAMLINE_ENABLED
        if (!streamlineAvailable) return extensions;

        sl::FeatureRequirements reqs{};
        sl::Result result = slGetFeatureRequirements(sl::kFeatureDLSS, reqs);
        if (result == sl::Result::eOk && reqs.vkNumInstanceExtensions > 0)
        {
            for (uint32_t i = 0; i < reqs.vkNumInstanceExtensions; ++i)
                extensions.push_back(reqs.vkInstanceExtensions[i]);
            vfLogInfo("Streamline requires {} instance extensions", reqs.vkNumInstanceExtensions);
        }
#endif
        return extensions;
    }

    std::vector<const char*> UpscaleManager::getRequiredDeviceExtensions()
    {
        std::vector<const char*> extensions;
#ifdef VF_STREAMLINE_ENABLED
        if (!streamlineAvailable) return extensions;

        sl::FeatureRequirements reqs{};
        sl::Result result = slGetFeatureRequirements(sl::kFeatureDLSS, reqs);
        if (result == sl::Result::eOk && reqs.vkNumDeviceExtensions > 0)
        {
            for (uint32_t i = 0; i < reqs.vkNumDeviceExtensions; ++i)
                extensions.push_back(reqs.vkDeviceExtensions[i]);
            vfLogInfo("Streamline requires {} device extensions", reqs.vkNumDeviceExtensions);
        }
#endif
        return extensions;
    }

    bool UpscaleManager::setVulkanDevice(core::Device& device)
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!streamlineAvailable) return false;

        // Vulkan calls go through sl.interposer.dll's proxy vkGetInstanceProcAddr,
        // so Streamline already tracks the VkInstance, VkDevice, and all resources.
        // No slSetVulkanInfo needed — the interposer handles it automatically.
        vfLogInfo("Streamline: Vulkan device set via interposer tracking");

        deviceSet = true;
        instance = this;
        queryFeatureSupport();

        vfLogInfo("Streamline Vulkan device set. DLSS={}",
                  dlssSupported ? "supported" : "not supported");
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
            // Free DLSS resources before shutdown to avoid leaked Vulkan objects
            if (deviceSet)
            {
                slFreeResources(sl::kFeatureDLSS_G, sl::ViewportHandle{0});
                slFreeResources(sl::kFeatureDLSS, sl::ViewportHandle{0});
            }
            if (reflexActive)
                applyReflexSettings({});
            slShutdown();
            streamlineAvailable = false;
            streamlineInitialized = false;
            deviceSet = false;
            instance = nullptr;
            vfLogInfo("Streamline SDK shut down");
        }
#endif
    }

    void UpscaleManager::freeFeatureResources()
    {
#ifdef VF_STREAMLINE_ENABLED
        if (deviceSet)
        {
            slFreeResources(sl::kFeatureDLSS_G, sl::ViewportHandle{0});
            slFreeResources(sl::kFeatureDLSS, sl::ViewportHandle{0});
            frameGenActive = false;
            vfLogInfo("Streamline: freed feature resources for resolution change");
        }
#endif
    }

    void UpscaleManager::queryFeatureSupport()
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!deviceSet) return;

        sl::AdapterInfo adapterInfo{};

        sl::Result dlssResult = slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo);
        dlssSupported = (dlssResult == sl::Result::eOk);
        vfLogInfo("Streamline DLSS support query: {} ({})",
                  slResultToString(dlssResult), static_cast<int>(dlssResult));

        sl::Result dlssGResult = slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo);
        dlssGSupported = (dlssGResult == sl::Result::eOk);
        vfLogInfo("Streamline DLSS-G (Frame Gen) support query: {} ({})",
                  slResultToString(dlssGResult), static_cast<int>(dlssGResult));

        sl::Result reflexResult = slIsFeatureSupported(sl::kFeatureReflex, adapterInfo);
        reflexSupported = (reflexResult == sl::Result::eOk);
        vfLogInfo("Streamline Reflex support query: {} ({})",
                  slResultToString(reflexResult), static_cast<int>(reflexResult));

        // Check if DLSS feature actually loaded
        if (dlssSupported)
        {
            bool loaded = false;
            sl::Result loadResult = slIsFeatureLoaded(sl::kFeatureDLSS, loaded);
            vfLogInfo("Streamline DLSS loaded: {} (query result: {})",
                      loaded ? "yes" : "no", slResultToString(loadResult));
        }

        // Check feature requirements for more details
        sl::FeatureRequirements reqs{};
        sl::Result reqResult = slGetFeatureRequirements(sl::kFeatureDLSS, reqs);
        if (reqResult == sl::Result::eOk)
        {
            vfLogInfo("Streamline DLSS requirements: flags=0x{:x}, computeQueues={}, graphicsQueues={}, vkExtensions={}",
                      static_cast<uint32_t>(reqs.flags),
                      reqs.vkNumComputeQueuesRequired,
                      reqs.vkNumGraphicsQueuesRequired,
                      reqs.vkNumDeviceExtensions);
            vfLogInfo("Streamline DLSS OS: detected={}.{}.{}, required={}.{}.{}",
                      reqs.osVersionDetected.major, reqs.osVersionDetected.minor, reqs.osVersionDetected.build,
                      reqs.osVersionRequired.major, reqs.osVersionRequired.minor, reqs.osVersionRequired.build);
            vfLogInfo("Streamline DLSS driver: detected={}.{}.{}, required={}.{}.{}",
                      reqs.driverVersionDetected.major, reqs.driverVersionDetected.minor, reqs.driverVersionDetected.build,
                      reqs.driverVersionRequired.major, reqs.driverVersionRequired.minor, reqs.driverVersionRequired.build);

            bool vulkanSupported = (static_cast<uint32_t>(reqs.flags) &
                                    static_cast<uint32_t>(sl::FeatureRequirementFlags::eVulkanSupported)) != 0;
            vfLogInfo("Streamline DLSS Vulkan supported flag: {}", vulkanSupported ? "yes" : "no");
        }
        else
        {
            vfLogWarning("Streamline DLSS requirements query failed: {} ({})",
                         slResultToString(reqResult), static_cast<int>(reqResult));
        }
#endif
    }

    ::postprocess::UpscaleMode UpscaleManager::resolveActiveMode(::postprocess::UpscaleMode requested) const
    {
        if (requested == ::postprocess::UpscaleMode::Off)
            return ::postprocess::UpscaleMode::Off;

        // DLSS and Auto both resolve to DLSS when supported
        if (requested == ::postprocess::UpscaleMode::DLSS ||
            requested == ::postprocess::UpscaleMode::Auto)
            return dlssSupported ? ::postprocess::UpscaleMode::DLSS : ::postprocess::UpscaleMode::Off;

        return ::postprocess::UpscaleMode::Off;
    }

    void UpscaleManager::applySettings(const ::postprocess::UpscaleSettings& settings,
                                        uint32_t outputWidth, uint32_t outputHeight)
    {
        activeMode = settings.enabled
            ? resolveActiveMode(settings.mode)
            : ::postprocess::UpscaleMode::Off;

        resolutionManager.setDisplayResolution(outputWidth, outputHeight);

        if (activeMode != ::postprocess::UpscaleMode::Off)
            resolutionManager.setQualityMode(settings.quality);
        else
            resolutionManager.setQualityMode(::postprocess::UpscaleQuality::Native);

#ifdef VF_STREAMLINE_ENABLED
        if (!deviceSet || activeMode == ::postprocess::UpscaleMode::Off) return;

        if (activeMode == ::postprocess::UpscaleMode::DLSS)
        {
            sl::DLSSOptions dlssOptions{};

            switch (settings.quality)
            {
            case ::postprocess::UpscaleQuality::Native:          dlssOptions.mode = sl::DLSSMode::eDLAA; break;
            case ::postprocess::UpscaleQuality::Quality:         dlssOptions.mode = sl::DLSSMode::eMaxQuality; break;
            case ::postprocess::UpscaleQuality::Balanced:        dlssOptions.mode = sl::DLSSMode::eBalanced; break;
            case ::postprocess::UpscaleQuality::Performance:     dlssOptions.mode = sl::DLSSMode::eMaxPerformance; break;
            case ::postprocess::UpscaleQuality::UltraPerformance:dlssOptions.mode = sl::DLSSMode::eUltraPerformance; break;
            }

            dlssOptions.outputWidth = outputWidth;
            dlssOptions.outputHeight = outputHeight;
            dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
            dlssOptions.useAutoExposure = sl::Boolean::eTrue;

            // Force transformer-based (DLTSS) presets for all quality modes.
            // Without this, Blackwell GPUs auto-select DLUnified (encoder/decoder)
            // for Performance/UltraPerf which requires denoiser inputs we don't provide.
            dlssOptions.dlaaPreset = sl::DLSSPreset::ePresetK;
            dlssOptions.qualityPreset = sl::DLSSPreset::ePresetK;
            dlssOptions.balancedPreset = sl::DLSSPreset::ePresetK;
            dlssOptions.performancePreset = sl::DLSSPreset::ePresetK;
            dlssOptions.ultraPerformancePreset = sl::DLSSPreset::ePresetK;

            slDLSSSetOptions(sl::ViewportHandle{0}, dlssOptions);
        }
#endif
    }

    bool UpscaleManager::evaluate(vk::CommandBuffer cmd, uint32_t frameIndex,
                                   const UpscaleInputs& inputs)
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!deviceSet || activeMode == ::postprocess::UpscaleMode::Off) return false;

        sl::Feature feature = sl::kFeatureDLSS;

        sl::ViewportHandle viewport{0};

        // Reuse the shared per-frame Reflex token (minted on the main thread, re-fetched
        // here by the published index) so DLSS constants/tags align with the Reflex frame
        // and we don't double-mint tokens that trigger "Setting constants multiple times".
        sl::FrameToken* frameToken =
            static_cast<sl::FrameToken*>(acquireFrameToken(lastKickedFrameIndex.load(std::memory_order_acquire)));
        if (!frameToken) return false;

        // Set constants — all SL matrices are row-major, GLM is column-major
        sl::Constants constants{};
        constants.jitterOffset = {inputs.jitterOffset.x, inputs.jitterOffset.y};
        constants.mvecScale = {0.5f, 0.5f};
        constants.reset = inputs.resetAccumulation ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        constants.depthInverted = sl::Boolean::eFalse; // Standard Z (near=0, far=1)
        constants.cameraPinholeOffset = {0.0f, 0.0f};

        // Camera matrices (must NOT contain jitter)
        constants.cameraViewToClip = toSL(inputs.projectionMatrix);
        constants.clipToCameraView = toSL(glm::inverse(inputs.projectionMatrix));

        // Reprojection: clipToPrevClip = invProj * invView * prevView * prevProj
        glm::mat4 clipToPrevClip = inputs.prevProjectionMatrix * inputs.prevViewMatrix
                                 * glm::inverse(inputs.viewMatrix)
                                 * glm::inverse(inputs.projectionMatrix);
        constants.clipToPrevClip = toSL(clipToPrevClip);
        constants.prevClipToClip = toSL(glm::inverse(clipToPrevClip));

        // Camera vectors from view matrix (inverse view = camera-to-world)
        glm::mat4 invView = glm::inverse(inputs.viewMatrix);
        constants.cameraPos = {invView[3][0], invView[3][1], invView[3][2]};
        constants.cameraRight = {invView[0][0], invView[0][1], invView[0][2]};
        constants.cameraUp = {invView[1][0], invView[1][1], invView[1][2]};
        constants.cameraFwd = {invView[2][0], invView[2][1], invView[2][2]};

        constants.cameraNear = inputs.nearPlane;
        constants.cameraFar = inputs.farPlane;
        float aspectRatio = static_cast<float>(inputs.renderExtent.width)
                          / static_cast<float>(inputs.renderExtent.height);
        constants.cameraAspectRatio = aspectRatio;
        constants.cameraFOV = 2.0f * std::atan(1.0f / inputs.projectionMatrix[1][1]);

        constants.cameraMotionIncluded = sl::Boolean::eTrue;
        constants.motionVectors3D = sl::Boolean::eFalse;
        constants.motionVectorsInvalidValue = 0.0f;
        constants.orthographicProjection = sl::Boolean::eFalse;
        constants.motionVectorsDilated = sl::Boolean::eFalse;
        constants.motionVectorsJittered = sl::Boolean::eFalse;

        slSetConstants(constants, *frameToken, viewport);

        // Tag resources
        sl::ResourceTag tags[7]{};
        uint32_t tagCount = 0;

        sl::Resource colorRes{sl::ResourceType::eTex2d, inputs.colorInput,
                              nullptr, static_cast<VkImageView>(inputs.colorView),
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        colorRes.width = inputs.renderExtent.width;
        colorRes.height = inputs.renderExtent.height;
        colorRes.nativeFormat = static_cast<uint32_t>(inputs.colorFormat);
        colorRes.mipLevels = 1;
        colorRes.arrayLayers = 1;
        tags[tagCount++] = sl::ResourceTag{&colorRes, sl::kBufferTypeScalingInputColor,
                                            sl::ResourceLifecycle::eValidUntilEvaluate, nullptr};

        // HUDLessColor — same as color input since UI is composited after upscaling
        tags[tagCount++] = sl::ResourceTag{&colorRes, sl::kBufferTypeHUDLessColor,
                                            sl::ResourceLifecycle::eValidUntilEvaluate, nullptr};

        sl::Resource depthRes{sl::ResourceType::eTex2d, inputs.depthInput,
                              nullptr, static_cast<VkImageView>(inputs.depthView),
                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        depthRes.width = inputs.renderExtent.width;
        depthRes.height = inputs.renderExtent.height;
        depthRes.nativeFormat = static_cast<uint32_t>(inputs.depthFormat);
        depthRes.mipLevels = 1;
        depthRes.arrayLayers = 1;
        tags[tagCount++] = sl::ResourceTag{&depthRes, sl::kBufferTypeDepth,
                                            sl::ResourceLifecycle::eValidUntilEvaluate, nullptr};

        sl::Resource mvecRes{sl::ResourceType::eTex2d, inputs.motionVectors,
                             nullptr, static_cast<VkImageView>(inputs.motionView),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        mvecRes.width = inputs.renderExtent.width;
        mvecRes.height = inputs.renderExtent.height;
        mvecRes.nativeFormat = static_cast<uint32_t>(inputs.motionFormat);
        mvecRes.mipLevels = 1;
        mvecRes.arrayLayers = 1;
        tags[tagCount++] = sl::ResourceTag{&mvecRes, sl::kBufferTypeMotionVectors,
                                            sl::ResourceLifecycle::eValidUntilEvaluate, nullptr};

        sl::Resource outputRes{sl::ResourceType::eTex2d, inputs.output,
                               nullptr, static_cast<VkImageView>(inputs.outputView),
                               VK_IMAGE_LAYOUT_GENERAL};
        outputRes.width = inputs.displayExtent.width;
        outputRes.height = inputs.displayExtent.height;
        outputRes.nativeFormat = static_cast<uint32_t>(inputs.outputFormat);
        outputRes.mipLevels = 1;
        outputRes.arrayLayers = 1;
        tags[tagCount++] = sl::ResourceTag{&outputRes, sl::kBufferTypeScalingOutputColor,
                                            sl::ResourceLifecycle::eValidUntilEvaluate, nullptr};

        if (inputs.reactiveMask)
        {
            sl::Resource reactiveRes{sl::ResourceType::eTex2d, inputs.reactiveMask,
                                     nullptr, static_cast<VkImageView>(inputs.reactiveView),
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            reactiveRes.width = inputs.renderExtent.width;
            reactiveRes.height = inputs.renderExtent.height;
            reactiveRes.nativeFormat = static_cast<uint32_t>(inputs.reactiveFormat);
            reactiveRes.mipLevels = 1;
            reactiveRes.arrayLayers = 1;
            tags[tagCount++] = sl::ResourceTag{&reactiveRes, sl::kBufferTypeTransparencyHint,
                                                sl::ResourceLifecycle::eValidUntilEvaluate, nullptr};
        }

        sl::Resource exposureRes{};
        if (inputs.exposureImage)
        {
            exposureRes = sl::Resource{sl::ResourceType::eTex2d, inputs.exposureImage,
                                       nullptr, static_cast<VkImageView>(inputs.exposureView),
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            exposureRes.width = 1;
            exposureRes.height = 1;
            exposureRes.nativeFormat = VK_FORMAT_R32_SFLOAT;
            exposureRes.mipLevels = 1;
            exposureRes.arrayLayers = 1;
            tags[tagCount++] = sl::ResourceTag{&exposureRes, sl::kBufferTypeExposure,
                                                sl::ResourceLifecycle::eValidUntilEvaluate, nullptr};
        }

        sl::Result tagResult = slSetTagForFrame(*frameToken, viewport, tags, tagCount,
                         reinterpret_cast<sl::CommandBuffer*>(static_cast<VkCommandBuffer>(cmd)));
        if (tagResult != sl::Result::eOk)
        {
            static bool loggedOnce = false;
            if (!loggedOnce)
            {
                vfLogWarning("Streamline setTag failed: {} ({}) — using bilinear fallback",
                             slResultToString(tagResult), static_cast<int>(tagResult));
                loggedOnce = true;
            }
            return false;
        }

        // Evaluate — viewport handle must be chained in inputs
        const sl::BaseStructure* evalInputs[] = {&viewport};
        sl::Result evalResult = slEvaluateFeature(feature, *frameToken, evalInputs, _countof(evalInputs),
                          reinterpret_cast<sl::CommandBuffer*>(static_cast<VkCommandBuffer>(cmd)));
        if (evalResult != sl::Result::eOk)
        {
            static bool loggedOnce = false;
            if (!loggedOnce)
            {
                vfLogError("Streamline evaluate failed: {} ({})",
                           slResultToString(evalResult), static_cast<int>(evalResult));
                loggedOnce = true;
            }
            return false;
        }
        return true;
#else
        return false;
#endif
    }

    void UpscaleManager::resetHistory()
    {
        // The next evaluate() call will pass resetAccumulation = true
        // which is handled via sl::Constants::reset in evaluate()
    }

    void UpscaleManager::applyReflexSettings(const ::postprocess::ReflexSettings& settings)
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!streamlineAvailable || !deviceSet || !reflexSupported)
        {
            reflexActive = false;
            activeReflexMode = ::postprocess::ReflexMode::Off;
            return;
        }

        sl::ReflexOptions options{};
        if (!settings.enabled)
        {
            options.mode = sl::ReflexMode::eOff;
        }
        else
        {
            options.mode = (settings.mode == ::postprocess::ReflexMode::OnBoost)
                ? sl::ReflexMode::eLowLatencyWithBoost
                : sl::ReflexMode::eLowLatency;
        }
        options.frameLimitUs = 0;
        options.useMarkersToOptimize = false;

        sl::Result result = slReflexSetOptions(options);
        if (result == sl::Result::eOk)
        {
            reflexActive = settings.enabled;
            activeReflexMode = settings.enabled ? settings.mode : ::postprocess::ReflexMode::Off;
        }
        else
        {
            reflexActive = false;
            activeReflexMode = ::postprocess::ReflexMode::Off;
        }
        vfLogInfo("Streamline Reflex: mode={} ({})",
                  !settings.enabled ? "Off"
                      : (settings.mode == ::postprocess::ReflexMode::OnBoost ? "On+Boost" : "On"),
                  slResultToString(result));
#endif
    }

    void* UpscaleManager::acquireFrameToken(uint32_t index)
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!streamlineAvailable) return nullptr;

        const uint32_t slot = index % kReflexTokenRing;
        // Serialize ring access: main and render threads can hit the same slot.
        std::lock_guard<std::mutex> lock(reflexTokenMutex);
        if (reflexTokenRing[slot] != nullptr && reflexTokenIndex[slot] == index)
            return reflexTokenRing[slot];

        sl::FrameToken* token = nullptr;
        uint32_t frameIndex = index;
        sl::Result result = slGetNewFrameToken(token, &frameIndex);
        if (result != sl::Result::eOk || !token)
            return nullptr;

        reflexTokenRing[slot] = token;
        reflexTokenIndex[slot] = index;
        return token;
#else
        (void)index;
        return nullptr;
#endif
    }

    void UpscaleManager::beginReflexFrame()
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!streamlineAvailable) return;
        // One token per main-loop iteration. fetch_add gives a monotonic index.
        uint32_t index = reflexFrameIndex.fetch_add(1, std::memory_order_acq_rel);
        acquireFrameToken(index);
#endif
    }

    void UpscaleManager::publishRenderFrameIndex()
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!streamlineAvailable) return;
        // The index minted by the most recent beginReflexFrame() is reflexFrameIndex-1.
        lastKickedFrameIndex.store(reflexFrameIndex.load(std::memory_order_acquire) - 1,
                                   std::memory_order_release);
#endif
    }

    void UpscaleManager::reflexSleep()
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!reflexActive) return;
        uint32_t index = reflexFrameIndex.load(std::memory_order_acquire) - 1;
        auto* token = static_cast<sl::FrameToken*>(acquireFrameToken(index));
        if (token)
            slReflexSleep(*token);
#endif
    }

#ifdef VF_STREAMLINE_ENABLED
    static sl::PCLMarker toPCLMarker(UpscaleManager::FrameMarker marker)
    {
        switch (marker)
        {
        case UpscaleManager::FrameMarker::InputPing:         return sl::PCLMarker::ePCLatencyPing;
        case UpscaleManager::FrameMarker::SimulationStart:   return sl::PCLMarker::eSimulationStart;
        case UpscaleManager::FrameMarker::SimulationEnd:     return sl::PCLMarker::eSimulationEnd;
        case UpscaleManager::FrameMarker::RenderSubmitStart: return sl::PCLMarker::eRenderSubmitStart;
        case UpscaleManager::FrameMarker::RenderSubmitEnd:   return sl::PCLMarker::eRenderSubmitEnd;
        case UpscaleManager::FrameMarker::PresentStart:      return sl::PCLMarker::ePresentStart;
        case UpscaleManager::FrameMarker::PresentEnd:        return sl::PCLMarker::ePresentEnd;
        }
        return sl::PCLMarker::ePCLatencyPing;
    }
#endif

    void UpscaleManager::setMarkerMain(FrameMarker marker)
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!reflexActive) return;
        uint32_t index = reflexFrameIndex.load(std::memory_order_acquire) - 1;
        auto* token = static_cast<sl::FrameToken*>(acquireFrameToken(index));
        if (token)
            slPCLSetMarker(toPCLMarker(marker), *token);
#else
        (void)marker;
#endif
    }

    void UpscaleManager::setMarkerRender(FrameMarker marker)
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!reflexActive) return;
        uint32_t index = lastKickedFrameIndex.load(std::memory_order_acquire);
        auto* token = static_cast<sl::FrameToken*>(acquireFrameToken(index));
        if (token)
            slPCLSetMarker(toPCLMarker(marker), *token);
#else
        (void)marker;
#endif
    }

    UpscaleManager::ReflexLatency UpscaleManager::getLatency() const
    {
        ReflexLatency latency{};
#ifdef VF_STREAMLINE_ENABLED
        if (!streamlineAvailable || !reflexActive) return latency;

        sl::ReflexState state{};
        if (slReflexGetState(state) != sl::Result::eOk || !state.latencyReportAvailable)
            return latency;

        // Pick the most recently completed frame (highest frameID) in the report ring.
        const sl::ReflexReport* report = nullptr;
        uint64_t newestFrameId = 0;
        for (int i = 0; i < sl::kReflexFrameReportCount; ++i)
        {
            if (state.frameReport[i].frameID >= newestFrameId)
            {
                newestFrameId = state.frameReport[i].frameID;
                report = &state.frameReport[i];
            }
        }
        if (!report)
            return latency;

        latency.gpuFrameTimeUs = report->gpuFrameTimeUs;
        if (report->presentEndTime != 0 && report->inputSampleTime != 0 &&
            report->presentEndTime > report->inputSampleTime)
        {
            latency.totalLatencyUs =
                static_cast<uint32_t>(report->presentEndTime - report->inputSampleTime);
        }
        latency.valid = (latency.gpuFrameTimeUs != 0 || latency.totalLatencyUs != 0);
#endif
        return latency;
    }

    void UpscaleManager::applyFrameGenSettings(const ::postprocess::FrameGenSettings& settings,
                                                uint32_t backBufferCount,
                                                uint32_t displayWidth, uint32_t displayHeight,
                                                uint32_t renderWidth, uint32_t renderHeight)
    {
#ifdef VF_STREAMLINE_ENABLED
        if (!deviceSet || !dlssGSupported)
        {
            if (settings.enabled)
                vfLogWarning("DLSS Frame Generation not supported on this GPU");
            frameGenActive = false;
            return;
        }

        // Reflex is configured by the caller (OffScreenController) after this call,
        // which forces Reflex On when Frame Gen ends up active. We only configure DLSS-G here.
        sl::DLSSGOptions options{};
        options.mode = settings.enabled ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
        options.numFramesToGenerate = std::clamp(settings.numFramesToGenerate, 1u, 3u);
        options.numBackBuffers = backBufferCount;
        options.colorWidth = displayWidth;
        options.colorHeight = displayHeight;
        options.mvecDepthWidth = renderWidth;
        options.mvecDepthHeight = renderHeight;

        sl::Result result = slDLSSGSetOptions(sl::ViewportHandle{0}, options);
        frameGenActive = (result == sl::Result::eOk && settings.enabled);

        if (settings.enabled)
        {
            vfLogInfo("DLSS Frame Generation: {} ({}), {}x frames",
                      frameGenActive ? "active" : "failed",
                      slResultToString(result),
                      settings.numFramesToGenerate + 1);
        }
        else
        {
            vfLogInfo("DLSS Frame Generation: disabled");
        }
#else
        frameGenActive = false;
#endif
    }
}
