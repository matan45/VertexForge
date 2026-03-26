#pragma once

#include "OceanFFTTypes.hpp"
#include <array>

namespace core
{
    class Device;
}

namespace render::water
{
    class OceanFFTResources
    {
    public:
        explicit OceanFFTResources(core::Device& device);
        ~OceanFFTResources();

        OceanFFTResources(const OceanFFTResources&) = delete;
        OceanFFTResources& operator=(const OceanFFTResources&) = delete;

        void init(uint32_t resolution);
        void cleanup();

        // Descriptor layouts
        [[nodiscard]] vk::DescriptorSetLayout getSpectrumDescLayout() const { return spectrumDescLayout; }
        [[nodiscard]] vk::DescriptorSetLayout getTimeEvolveDescLayout() const { return timeEvolveDescLayout; }
        [[nodiscard]] vk::DescriptorSetLayout getFFTDescLayout() const { return fftDescLayout; }
        [[nodiscard]] vk::DescriptorSetLayout getMergeDescLayout() const { return mergeDescLayout; }
        [[nodiscard]] vk::DescriptorSetLayout getOceanTextureDescLayout() const { return oceanTextureDescLayout; }

        // Descriptor sets
        [[nodiscard]] vk::DescriptorSet getSpectrumDescSet() const { return spectrumDescSet; }
        [[nodiscard]] vk::DescriptorSet getTimeEvolveDescSet() const { return timeEvolveDescSet; }
        [[nodiscard]] const std::array<vk::DescriptorSet, 6>& getFFTDescSets() const { return fftDescSets; }
        [[nodiscard]] vk::DescriptorSet getMergeDescSet() const { return mergeDescSet; }
        [[nodiscard]] vk::DescriptorSet getOceanTextureDescSet() const { return oceanTextureDescSet; }

        // Image handles
        [[nodiscard]] vk::Image getDisplacementImage() const { return displacementImage; }
        [[nodiscard]] vk::Image getNormalImage() const { return normalImage; }
        [[nodiscard]] vk::Image getCausticImage() const { return causticImage; }
        [[nodiscard]] vk::ImageView getCausticView() const { return causticView; }

    private:
        core::Device& device;

        // h0 spectrum texture (RGBA32F)
        vk::Image h0Image;
        vk::DeviceMemory h0Memory;
        vk::ImageView h0View;

        // Field textures: 3 fields (Dy, Dx, Dz) x 2 ping-pong each (RG32F)
        std::array<FieldPair, 3> fields;

        // Output textures (RGBA16F)
        vk::Image displacementImage;
        vk::DeviceMemory displacementMemory;
        vk::ImageView displacementView;

        vk::Image normalImage;
        vk::DeviceMemory normalMemory;
        vk::ImageView normalView;

        vk::Image causticImage;
        vk::DeviceMemory causticMemory;
        vk::ImageView causticView;

        vk::Sampler outputSampler;

        // Descriptor set layouts
        vk::DescriptorSetLayout spectrumDescLayout;
        vk::DescriptorSetLayout timeEvolveDescLayout;
        vk::DescriptorSetLayout fftDescLayout;
        vk::DescriptorSetLayout mergeDescLayout;
        vk::DescriptorSetLayout oceanTextureDescLayout;

        // Descriptor pool and sets
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet spectrumDescSet;
        vk::DescriptorSet timeEvolveDescSet;
        std::array<vk::DescriptorSet, 6> fftDescSets;
        vk::DescriptorSet mergeDescSet;
        vk::DescriptorSet oceanTextureDescSet;

        void createTextures(uint32_t resolution);
        void createSampler();
        void createDescriptorLayouts();
        void createDescriptorPool();
        void allocateDescriptorSets();
        void updateDescriptorSets();
        void transitionImagesInitial();
        void destroyTextures();
    };
}
