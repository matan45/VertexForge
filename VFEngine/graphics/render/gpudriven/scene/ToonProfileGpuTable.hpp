#pragma once

#include <vulkan/vulkan.hpp>
#include "../../../core/VulkanMemoryManager.hpp"   // core::VulkanAllocation
#include "material/ToonProfile.hpp"
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace core { class Device; }

namespace render::gpudriven
{
    // std430 GPU row, 96 bytes / six vec4. MUST match struct ToonProfileGPU in
    // resources/shaders/common/toon_shading.glsl.
    struct alignas(16) ToonProfileGPU
    {
        glm::vec4 shadeColor;       // rgb = deepest-shadow tint            (a unused)
        glm::vec4 midColor;         // rgb = mid-band tint                  (a unused)
        glm::vec4 diffParams;       // x=shadowThreshold y=midThreshold z=bandSmoothness w=giScale
        glm::vec4 specParams;       // x=specThreshold  y=specSmoothness   z=specIntensity w=specShininess
        glm::vec4 specColorRimPow;  // xyz=specColor                        w=rimPower
        glm::vec4 rimParams;        // xyz=rimColor                         w=rimIntensity
    };
    static_assert(sizeof(ToonProfileGPU) == 96, "ToonProfileGPU must be 96 bytes to match the GLSL std430 layout");

    inline constexpr uint32_t TOON_PROFILE_TABLE_CAPACITY = 128;  // == ProfileIndexMask + 1
    inline constexpr uint8_t  TOON_PROFILE_DEFAULT_INDEX  = 0;    // built-in default, always present

    // Fixed-capacity device SSBO of toon profiles, bound at set 1 / binding 6 of the
    // scene mesh pipeline. Owned by GPUDrivenRenderer. The device buffer is created
    // once and its handle is stable for the renderer's lifetime, which is what makes
    // the always-declared binding 6 validation-clean without ePartiallyBound: slot 0
    // holds a valid default from frame 0, so the buffer is a live non-empty SSBO on
    // every draw. Subscribes to ToonProfileManager so profile edits re-upload the
    // matching row in place (index-stable → every referencing object updates live).
    class ToonProfileGpuTable
    {
    public:
        explicit ToonProfileGpuTable(core::Device& device);
        ~ToonProfileGpuTable();
        ToonProfileGpuTable(const ToonProfileGpuTable&) = delete;
        ToonProfileGpuTable& operator=(const ToonProfileGpuTable&) = delete;

        void init();     // allocate buffers, write built-in default into slot 0, subscribe
        void cleanup();  // unsubscribe + destroy buffers (call with device idle)

        // Assign/return the 0..127 slot for a profile path. Slot 0 is the built-in
        // default, returned (with a one-time warning) on empty/unknown/over-capacity.
        // Call on the MAIN THREAD during the sequential extractor pre-warm; the mutex
        // guards the rare concurrent read during the parallel object phase.
        uint8_t resolveIndex(const std::string& profilePath);

        void markDirty() { std::lock_guard<std::mutex> lock(mutex_); dirty_ = true; }

        // Record a staging copy + barrier into cmd if the mirror changed. Cheap
        // (12 KB) — safe to call every frame alongside the SVT upload.
        void uploadIfDirty(vk::CommandBuffer cmd);

        vk::Buffer getBuffer() const { return deviceBuffer_; }  // STABLE for lifetime

        // The single active table, so the static MaterialPBRExtractor can resolve
        // indices during extraction. Set by GPUDrivenRenderer::init, cleared on teardown.
        static ToonProfileGpuTable* active() { return s_active; }
        static void setActive(ToonProfileGpuTable* t) { s_active = t; }

    private:
        static void fillRow(ToonProfileGPU& row, const material::ToonProfile& p);
        void loadInto(uint8_t slot, const std::string& path);   // caller holds mutex_
        void onProfileChanged(const std::string& path);

        core::Device& device_;

        vk::Buffer deviceBuffer_;
        core::VulkanAllocation deviceAlloc_;
        vk::Buffer stagingBuffer_;
        core::VulkanAllocation stagingAlloc_;

        std::array<ToonProfileGPU, TOON_PROFILE_TABLE_CAPACITY> cpuMirror_{};
        std::unordered_map<std::string, uint8_t> pathToIndex_;
        uint8_t nextIndex_ = 1;              // 0 reserved for the built-in default
        bool warnedOverflow_ = false;
        bool dirty_ = true;
        bool initialized_ = false;
        uint64_t changeCallbackId_ = 0;
        mutable std::mutex mutex_;

        static ToonProfileGpuTable* s_active;
    };
}
