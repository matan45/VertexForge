#pragma once

#include <vulkan/vulkan.hpp>

namespace render::raytracing
{
    // Canonical "denoised mask" sampler/layout builders shared by the RT shadow denoisers and
    // upsample pipelines. The denoised-mask consumer (set 13/15/16, fragment stage) and the
    // half-res guide reads (compute) must use byte-identical sampler state and descriptor layout
    // across all of RTShadowDenoiser, RTLayeredShadowDenoiser, RTShadowUpsamplePipeline and
    // RTLayeredShadowUpsamplePipeline — a producer swap in the VK-1398 set-13 ring is only a clean
    // vkCopyDescriptorSets repoint if these match exactly. Factoring them here makes that contract
    // a single source of truth instead of four hand-copied blocks.
    //
    // Each factory returns a FRESH handle per call; the caller owns and destroys it (preserving the
    // per-instance ownership / cleanup order the classes already rely on — no shared/cached object).

    // Linear filter, clampToEdge on all axes — the fragment-stage denoised-mask read sampler.
    vk::Sampler createDenoisedMaskSampler(const vk::Device& device);

    // Nearest filter, clampToEdge on all axes — the guide sampler for depth/normal/half-mask reads.
    vk::Sampler createGuideSampler(const vk::Device& device);

    // The denoised-mask consumer layout: binding 0, combined image sampler, count 1, fragment stage.
    vk::DescriptorSetLayout createDenoisedMaskLayout(const vk::Device& device);
}
