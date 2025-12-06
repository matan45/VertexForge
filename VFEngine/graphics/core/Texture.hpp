#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

namespace core {
	class Device;

	struct ImageData
	{
		uint32_t width;
		uint32_t height;
		uint32_t numbersOfChannels;
	};

	class Texture
	{
	private:
		Device& device;
		vk::UniqueCommandPool commandPool;

		vk::Image image;
		vk::DeviceMemory imageMemory;
		vk::ImageView imageView;
		ImageData imageData;

		vk::Sampler sampler;

		vk::DescriptorSet descriptorSet;
		vk::UniqueDescriptorPool descriptorPool;

		bool isEditorTexture = false;

	public:
		explicit Texture(Device& device);
		~Texture();

		void loadTextureFromFile(std::string_view filePath, vk::Format format = vk::Format::eR8G8B8A8Srgb, bool isEditor = true);
		void loadHDRFromFile(std::string_view filePath, vk::Format format = vk::Format::eR32G32B32Sfloat, bool isEditor = true);
		const vk::DescriptorSet& getDescriptorSet() const { return descriptorSet; }
		const vk::ImageView& getImageView() const { return imageView; }
		const vk::Sampler& getSampler() const { return sampler; }
		const ImageData& getImageData() const { return imageData; }

	private:
		void createSampler();
		void copyBufferToImage(vk::Buffer buffer, uint32_t width, uint32_t height);
	};
}


