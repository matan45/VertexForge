#pragma once
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <cstdint>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::occlusion
{
    class DepthPrepass
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        vk::Image depthImage;
        core::VulkanAllocation depthAllocation;
        vk::ImageView depthImageView;

        vk::Image normalImage;
        core::VulkanAllocation normalAllocation;
        vk::ImageView normalImageView;

        // DLSS-D Ray Reconstruction demodulation guides (VK-1397). Created only while
        // Ray Reconstruction is the active upscaler (setAlbedoEnabled), so the default
        // render path pays nothing.
        vk::Image diffuseAlbedoImage;
        core::VulkanAllocation diffuseAlbedoAllocation;
        vk::ImageView diffuseAlbedoImageView;
        vk::Image specularAlbedoImage;
        core::VulkanAllocation specularAlbedoAllocation;
        vk::ImageView specularAlbedoImageView;
        bool albedoEnabled = false;

        uint32_t width = 0;
        uint32_t height = 0;
        bool initialized = false;

        static constexpr vk::Format DEPTH_FORMAT = vk::Format::eD32Sfloat;
        static constexpr vk::Format NORMAL_FORMAT = vk::Format::eR16G16B16A16Sfloat;
        static constexpr vk::Format ALBEDO_FORMAT = vk::Format::eR16G16B16A16Sfloat;

    public:
        explicit DepthPrepass(core::Device& device, core::SwapChain& swapChain);
        ~DepthPrepass();

        DepthPrepass(const DepthPrepass&) = delete;
        DepthPrepass& operator=(const DepthPrepass&) = delete;

        void init(uint32_t width, uint32_t height);
        void cleanup();

        void beginPass(vk::CommandBuffer cmd) const;
        void endPass(vk::CommandBuffer cmd) const;

        // Toggle the Ray Reconstruction albedo guide targets (VK-1397). Allocates the
        // diffuse/specular albedo images on enable and frees them on disable; idles the
        // device first since it (de)allocates GPU resources mid-session.
        void setAlbedoEnabled(bool enabled);
        bool isAlbedoEnabled() const { return albedoEnabled; }

        vk::Image getDepthImage() const { return depthImage; }
        vk::ImageView getDepthImageView() const { return depthImageView; }
        vk::Image getNormalImage() const { return normalImage; }
        vk::ImageView getNormalImageView() const { return normalImageView; }
        vk::Image getDiffuseAlbedoImage() const { return diffuseAlbedoImage; }
        vk::ImageView getDiffuseAlbedoImageView() const { return diffuseAlbedoImageView; }
        vk::Image getSpecularAlbedoImage() const { return specularAlbedoImage; }
        vk::ImageView getSpecularAlbedoImageView() const { return specularAlbedoImageView; }
        uint32_t getWidth() const { return width; }
        uint32_t getHeight() const { return height; }
        bool isInitialized() const { return initialized; }

        static constexpr vk::Format getDepthFormat() { return DEPTH_FORMAT; }
        static constexpr vk::Format getNormalFormat() { return NORMAL_FORMAT; }
        static constexpr vk::Format getAlbedoFormat() { return ALBEDO_FORMAT; }

    private:
        void createImages();
        void createAlbedoImages();
        void destroyAlbedoImages();
        void transitionInitialLayouts();
    };
}
