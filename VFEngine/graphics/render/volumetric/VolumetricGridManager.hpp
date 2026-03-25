#pragma once

#include "VolumetricTypes.hpp"
#include "FogNoiseGenerator.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
}

namespace render::volumetric
{
    class VolumetricGridManager
    {
    private:
        core::Device& device;
        VolumetricGridDimensions dims{};

        // Scattering volume (written by injection, read by temporal)
        vk::Image scatteringImage;
        vk::DeviceMemory scatteringMemory;
        vk::ImageView scatteringView;

        // History volumes (ping-pong for temporal reprojection)
        vk::Image historyImages[2];
        vk::DeviceMemory historyMemory[2];
        vk::ImageView historyViews[2];
        uint32_t currentHistoryIndex = 0;

        // Integrated output (written by ray march, read by composite)
        vk::Image integratedImage;
        vk::DeviceMemory integratedMemory;
        vk::ImageView integratedView;

        vk::Sampler trilinearSampler;

        // Fog noise texture generator
        std::unique_ptr<FogNoiseGenerator> fogNoise;

        vk::Buffer paramsBuffer;
        vk::DeviceMemory paramsMemory;
        void* paramsMapped = nullptr;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSets[2]; // double-buffered for history ping-pong

        bool initialized = false;

        void create3DImage(vk::Image& image, vk::DeviceMemory& memory,
                           vk::ImageView& view, vk::ImageUsageFlags usage);
        void destroy3DImage(vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view);

        void createImages();
        void transitionImagesToGeneral();
        void destroyImages();
        void createSampler();
        void createParamsBuffer();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void writeDescriptors();

    public:
        explicit VolumetricGridManager(core::Device& device);
        ~VolumetricGridManager();

        VolumetricGridManager(const VolumetricGridManager&) = delete;
        VolumetricGridManager& operator=(const VolumetricGridManager&) = delete;

        void init(VolumetricQuality quality);
        void cleanup();
        void recreate(VolumetricQuality quality);

        void swapHistory();
        void updateParams(const GPUVolumetricParams& params);

        [[nodiscard]] vk::ImageView getScatteringView() const { return scatteringView; }
        [[nodiscard]] vk::Image getScatteringImage() const { return scatteringImage; }
        [[nodiscard]] vk::ImageView getCurrentHistoryView() const { return historyViews[currentHistoryIndex]; }
        [[nodiscard]] vk::Image getCurrentHistoryImage() const { return historyImages[currentHistoryIndex]; }
        [[nodiscard]] vk::ImageView getPreviousHistoryView() const { return historyViews[1 - currentHistoryIndex]; }
        [[nodiscard]] vk::Image getPreviousHistoryImage() const { return historyImages[1 - currentHistoryIndex]; }
        [[nodiscard]] vk::ImageView getIntegratedView() const { return integratedView; }
        [[nodiscard]] vk::Image getIntegratedImage() const { return integratedImage; }
        [[nodiscard]] vk::Sampler getSampler() const { return trilinearSampler; }

        [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        [[nodiscard]] vk::DescriptorSet getDescriptorSet() const { return descriptorSets[currentHistoryIndex]; }

        [[nodiscard]] const VolumetricGridDimensions& getDimensions() const { return dims; }
        [[nodiscard]] bool isInitialized() const { return initialized; }
    };
}
