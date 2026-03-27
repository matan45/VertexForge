#include "ImguiRender.hpp"
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>
#include <IconsFontAwesome6.h>

#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/CommandPool.hpp"
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
		createRenderPass();
		createFrameBuffers();

		// Setup Platform/Renderer back ends
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
		initInfo.PipelineInfoMain.RenderPass = imGuiRenderPass;
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

		theme();
	}

	void ImguiRender::cleanUp() const
	{
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		for (auto framebuffer : imGuiFrameBuffers) {
			device.getLogicalDevice().destroyFramebuffer(framebuffer);
		}

		// Destroy the graphics pipeline, pipeline layout, and render pass
		device.getLogicalDevice().destroyRenderPass(imGuiRenderPass);
		device.getLogicalDevice().destroyDescriptorPool(imGuiDescriptorPool);
	}

	void ImguiRender::recreate()
	{
		for (auto framebuffer : imGuiFrameBuffers) {
			device.getLogicalDevice().destroyFramebuffer(framebuffer);
		}

		// Destroy the graphics pipeline, pipeline layout, and render pass
		device.getLogicalDevice().destroyRenderPass(imGuiRenderPass);

		createRenderPass();
		createFrameBuffers();
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

		// Record an empty render pass to transition swapchain image to present layout
		vk::ClearValue clearColor = { std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f} };

		vk::RenderPassBeginInfo renderPassinfo = {};
		renderPassinfo.renderPass = imGuiRenderPass;
		renderPassinfo.framebuffer = imGuiFrameBuffers[imageIndex];
		renderPassinfo.renderArea.extent.width = swapChain.getSwapchainExtent().width;
		renderPassinfo.renderArea.extent.height = swapChain.getSwapchainExtent().height;
		renderPassinfo.clearValueCount = 1;
		renderPassinfo.pClearValues = &clearColor;

		commandBuffer.beginRenderPass(renderPassinfo, vk::SubpassContents::eInline);
		commandBuffer.endRenderPass();
	}

	void ImguiRender::renderFromSnapshot(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
	                                      ImDrawData* snapshotDrawData) const
	{
		if (!snapshotDrawData)
			return;

		vk::ClearValue clearColor = { std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f} };

		vk::RenderPassBeginInfo renderPassinfo = {};
		renderPassinfo.renderPass = imGuiRenderPass;
		renderPassinfo.framebuffer = imGuiFrameBuffers[imageIndex];
		renderPassinfo.renderArea.extent.width = swapChain.getSwapchainExtent().width;
		renderPassinfo.renderArea.extent.height = swapChain.getSwapchainExtent().height;
		renderPassinfo.clearValueCount = 1;
		renderPassinfo.pClearValues = &clearColor;

		commandBuffer.beginRenderPass(renderPassinfo, vk::SubpassContents::eInline);

		ImGui_ImplVulkan_RenderDrawData(snapshotDrawData, commandBuffer);

		commandBuffer.endRenderPass();
	}

	void ImguiRender::theme() const
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);            // White text
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);    // Gray for disabled text
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.20f, 1.00f);        // Midnight blue background
		style.Colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.20f, 1.00f);         // Same as WindowBg
		style.Colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.24f, 1.00f);         // Slightly brighter midnight blue for popups
		style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);          // Light border, same as default
		style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);    // No shadow
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.30f, 1.00f);         // Darker blue for frames
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.50f, 1.00f);  // Brighter on hover
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.40f, 0.70f, 1.00f);   // Active frame is lighter blue
		style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.16f, 1.00f);         // Midnight blue for title bar
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.10f, 0.20f, 1.00f);   // Active title bar in Midnight Blue
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.05f, 0.10f, 1.00f);// Collapsed state is very dark blue
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.10f, 0.20f, 1.00f);       // Menu bar in Midnight Blue
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.05f, 0.53f);     // Almost black background for scrollbar
		style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.50f, 0.50f, 0.10f, 1.00f);   // Gold scrollbar
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.70f, 0.70f, 0.20f, 1.00f); // Bright Gold when hovered
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.90f, 0.90f, 0.25f, 1.00f);  // Active state for scrollbar in bright Gold
		style.Colors[ImGuiCol_CheckMark] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);       // Gold check mark
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);      // Gold slider grab
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.85f, 0.00f, 1.00f);// Brighter Gold when active
		style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.30f, 1.00f);          // Midnight blue button
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.50f, 1.00f);   // Brighter on hover
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);    // Gold when active
		style.Colors[ImGuiCol_Header] = ImVec4(0.9f, 0.3f, 0.3f, 0.8f);          // Midnight blue header
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.20f, 0.40f, 1.00f);   // Lighter blue on hover
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);    // Gold when active
		style.Colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);       // Default separator color
		style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);// Gold when hovered
		style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f); // Gold when active
		style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.10f, 0.10f, 0.20f, 1.00f);      // Midnight blue resize grip
		style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);// Gold when hovered
		style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);// Gold when active
		style.Colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.10f, 0.20f, 1.00f);             // Midnight blue tabs
		style.Colors[ImGuiCol_TabHovered] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);      // Gold when hovered
		style.Colors[ImGuiCol_TabActive] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);       // Gold when active
		style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.05f, 0.05f, 0.10f, 1.00f);    // Dark blue for unfocused tabs
		style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.25f, 0.25f, 0.50f, 1.00f);// Light blue for active but unfocused tabs
		style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.90f, 0.75f, 0.10f, 0.70f);  // Gold for docking preview
		style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.10f, 0.10f, 0.20f, 1.00f);  // Midnight blue for docking background
		style.Colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);       // Default plot line color
		style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);// Gold on hover
		style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);   // Gold for histograms
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.85f, 0.00f, 1.00f);// Brighter gold when hovered
		style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.90f, 0.75f, 0.10f, 0.35f);  // Gold for selected text background
		style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);  // Gold for drag and drop target
		style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.90f, 0.75f, 0.10f, 1.00f);    // Gold for navigation highlight
		style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f); // Bright white for windowing highlight
		style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);// Dim background for windowing
		style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.10f, 0.10f, 0.20f, 0.35f);// Midnight blue for modal window dim background
		style.GrabRounding = style.FrameRounding = 2.3f;                             // Slightly rounded edges

	}

	void ImguiRender::createRenderPass()
	{
		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = swapChain.getSwapchainImageFormat();
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

		vk::AttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		vk::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;

		imGuiRenderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
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

	void ImguiRender::createFrameBuffers()
	{
		imGuiFrameBuffers.resize(swapChain.getImageCount());

		for (uint32_t i = 0; i < imGuiFrameBuffers.size(); i++) {
			vk::ImageView viewImage = swapChain.getSwapchainImageView(i);

			vk::FramebufferCreateInfo framebufferInfo{};
			framebufferInfo.renderPass = imGuiRenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = &viewImage;
			framebufferInfo.width = swapChain.getSwapchainExtent().width;
			framebufferInfo.height = swapChain.getSwapchainExtent().height;
			framebufferInfo.layers = 1;

			imGuiFrameBuffers[i] = device.getLogicalDevice().createFramebuffer(framebufferInfo);
		}
	}

}