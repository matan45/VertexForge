#include "ImguiRender.hpp"
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>
#include <IconsFontAwesome6.h>

#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/CommandPool.hpp"
#include "../core/DynamicRenderingHelpers.hpp"
#include "../window/Window.hpp"
#include "../core/Utilities.hpp"
#include "print/Log.hpp"

namespace imguiPass {

	ImguiRender::ImguiRender(core::Device& device, core::SwapChain& swapChain, core::CommandPool& commandPool,const window::Window* window) :
		device{ device }, swapChain{ swapChain }, commandPool{ commandPool }, window{ window }
	{

	}

	void ImguiRender::init()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		createDescriptorPool();

		// Setup Platform/Renderer back ends with dynamic rendering
		ImGui_ImplGlfw_InitForVulkan(window->getWindowPtr(), true);
		ImGui_ImplVulkan_InitInfo initInfo{};
		initInfo.ApiVersion = VK_API_VERSION_1_3;
		initInfo.Instance = device.getInstance();
		initInfo.PhysicalDevice = device.getPhysicalDevice();
		initInfo.Device = device.getLogicalDevice();
		initInfo.QueueFamily = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
		initInfo.Queue = device.getGraphicsQueue();
		initInfo.PipelineCache = device.getPipelineCache();
		initInfo.DescriptorPool = imGuiDescriptorPool;
		initInfo.MinImageCount = swapChain.getImageCount();
		initInfo.ImageCount = swapChain.getImageCount();
		initInfo.Allocator = VK_NULL_HANDLE;
		initInfo.UseDynamicRendering = true;

		VkFormat swapFormat = static_cast<VkFormat>(swapChain.getSwapchainImageFormat());
		VkPipelineRenderingCreateInfoKHR pipelineRendering{};
		pipelineRendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		pipelineRendering.colorAttachmentCount = 1;
		pipelineRendering.pColorAttachmentFormats = &swapFormat;
		initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRendering;
		initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

		ImGui_ImplVulkan_Init(&initInfo);

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking

		// Load a custom font
		io.Fonts->AddFontFromFileTTF("../../resources/editor/Roboto-Regular.ttf", 18.0f);

		// Font Awesome icon font configuration
		const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 }; // Icon range

		ImFontConfig config;
		config.MergeMode = true; // Merge icon font with the default one
		config.PixelSnapH = true;

		// Load Font Awesome font (ensure the path points to the Font Awesome .ttf file)
		io.Fonts->AddFontFromFileTTF("../../resources/editor/fa-solid-900.ttf", 16.0f, &config, icons_ranges);

		ImGui::StyleColorsDark();
	}

	void ImguiRender::cleanUp() const
	{
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		device.getLogicalDevice().destroyDescriptorPool(imGuiDescriptorPool);
	}

	void ImguiRender::recreate()
	{
		// Nothing to recreate - dynamic rendering uses swapchain image views directly
	}

	void ImguiRender::render(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
	{
		ImGui::Render();
		ImDrawData* drawData = ImGui::GetDrawData();

		renderFromSnapshot(commandBuffer, imageIndex, drawData);

		drawData->Clear();
	}

	void ImguiRender::generateAndSnapshotDrawData()
	{
		ImGui::Render();
		ImDrawData* drawData = ImGui::GetDrawData();

		// Write to the current write slot (render thread reads from the other slot)
		uint32_t wi = writeIndex.load(std::memory_order_relaxed);
		snapshots[wi].snapshot(drawData);

		// Publish: make the write slot the new read slot
		readIndex.store(wi, std::memory_order_release);

		// Flip write slot for next frame
		writeIndex.store(1 - wi, std::memory_order_relaxed);

		drawData->Clear();
	}

	void ImguiRender::renderSnapshotted(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
	{
		uint32_t ri = readIndex.load(std::memory_order_acquire);
		ImDrawData* snapshotData = snapshots[ri].getDrawData();

		if (snapshotData)
		{
			renderFromSnapshot(commandBuffer, imageIndex, snapshotData);
		}
		else
		{
			renderEmpty(commandBuffer, imageIndex);
		}
	}

	void ImguiRender::renderEmpty(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
	{
		vk::Image swapchainImage = swapChain.getSwapchainImage(imageIndex);

		core::transitionSwapchainForRendering(commandBuffer, swapchainImage);

		auto colorAttach = core::colorClear(swapChain.getSwapchainImageView(imageIndex),
			vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});

		core::DynamicRenderingInfo dynInfo{};
		dynInfo.extent = swapChain.getDisplayExtent();
		dynInfo.colorAttachments = {colorAttach};

		core::beginDynamicRendering(commandBuffer, dynInfo);
		core::endDynamicRendering(commandBuffer);

		core::transitionSwapchainForPresent(commandBuffer, swapchainImage);
	}

	void ImguiRender::renderFromSnapshot(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
	                                      ImDrawData* snapshotDrawData) const
	{
		if (!snapshotDrawData)
			return;

		vk::Image swapchainImage = swapChain.getSwapchainImage(imageIndex);

		core::transitionSwapchainForRendering(commandBuffer, swapchainImage);

		auto colorAttach = core::colorClear(swapChain.getSwapchainImageView(imageIndex),
			vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}});

		core::DynamicRenderingInfo dynInfo{};
		dynInfo.extent = swapChain.getDisplayExtent();
		dynInfo.colorAttachments = {colorAttach};

		core::beginDynamicRendering(commandBuffer, dynInfo);

		ImGui_ImplVulkan_RenderDrawData(snapshotDrawData, commandBuffer);

		core::endDynamicRendering(commandBuffer);

		core::transitionSwapchainForPresent(commandBuffer, swapchainImage);
	}


	void ImguiRender::createDescriptorPool()
	{
		std::vector<vk::DescriptorPoolSize> poolSizes =
		{
			{ vk::DescriptorType::eSampler, 1000 },
			{ vk::DescriptorType::eCombinedImageSampler, 1000 },
			{ vk::DescriptorType::eSampledImage, 1000 },
			{ vk::DescriptorType::eStorageImage, 1000 },
			{ vk::DescriptorType::eUniformTexelBuffer , 1000 },
			{ vk::DescriptorType::eStorageTexelBuffer, 1000 },
			{ vk::DescriptorType::eUniformBuffer, 1000 },
			{ vk::DescriptorType::eStorageBuffer, 1000 },
			{ vk::DescriptorType::eUniformBufferDynamic, 1000 },
			{ vk::DescriptorType::eStorageBufferDynamic, 1000 },
			{ vk::DescriptorType::eInputAttachment , 1000 }
		};

		vk::DescriptorPoolCreateInfo poolInfo = {};
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = static_cast <uint32_t>(1000 * poolSizes.size());
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

		try
		{
			imGuiDescriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
		}
		catch (vk::SystemError& err)
		{
			vfLogError("failed to create DescriptorPool {}", err.what());
		}
	}

}
