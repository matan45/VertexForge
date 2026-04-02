#include "ImagePreviewWindow.hpp"
#include "imgui.h"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "resource/ResourceManager.hpp"
#include "asset/AssetRef.hpp"
#include "TextureCompressor.hpp"
#include <glm/glm.hpp>
#include <filesystem>
#include <cmath>

namespace windows
{
    ImagePreviewWindow::ImagePreviewWindow(const std::string& filePath, bool hdr)
        : imagePath(filePath)
          , isHDR(hdr)
    {
        std::filesystem::path path(filePath);
        windowTitle = (hdr ? "HDR Preview: " : "Image Preview: ") + path.filename().string();
    }

    ImagePreviewWindow::~ImagePreviewWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (loadingProgress.isLoading())
        {
            events::render::CancelTextureLoadingCommand cancelCmd;
            cancelCmd.instanceId = this;
            dispatcher.execute(cancelCmd);
        }

        // Wait for pending channel future to avoid dangling state
        if (channelFuture.valid())
        {
            channelFuture.wait();
        }

        releaseChannelTexture();

        if (imageHandle.isValid())
        {
            events::render::ReleaseEditorTextureCommand releaseCmd;
            releaseCmd.handle = imageHandle.imguiDescriptorSet;
            dispatcher.execute(releaseCmd);
        }
    }

    void ImagePreviewWindow::draw()
    {
        if (!isOpen)
        {
            return;
        }

        if (needsInit)
        {
            loadImageAsync();
            needsInit = false;
        }

        updateAsyncLoading();
        updateChannelLoading();

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                float panelWidth = 150.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();

                ImGui::BeginChild("InfoPanel", ImVec2(panelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine();

                float viewportWidth = contentSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;
                ImGui::BeginChild("ImagePanel", ImVec2(viewportWidth, contentSize.y), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                if (loadingProgress.isLoading())
                {
                    ImVec2 size = ImGui::GetContentRegionAvail();
                    drawLoadingIndicator(size.x, size.y);
                }
                else
                {
                    drawImagePanel();
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void ImagePreviewWindow::loadImageAsync()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::render::LoadEditorTextureAsyncCommand loadCmd;
        loadCmd.instanceId = this;
        loadCmd.path = imagePath;
        loadCmd.isHDR = isHDR;
        dispatcher.execute(loadCmd);

        loadingProgress.state = services::LoadingState::Pending;
        loadingProgress.statusMessage = isHDR ? "Loading HDR texture..." : "Loading texture...";
    }

    void ImagePreviewWindow::updateAsyncLoading()
    {
        if (!loadingProgress.isLoading())
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        events::render::GetTextureLoadingProgressQuery progressQuery;
        progressQuery.instanceId = this;
        loadingProgress = dispatcher.query(progressQuery);

        if (loadingProgress.state == services::LoadingState::Complete)
        {
            events::render::GetLoadedTextureHandleQuery handleQuery;
            handleQuery.instanceId = this;
            imageHandle = dispatcher.query(handleQuery);
        }
    }

    void ImagePreviewWindow::updateChannelLoading()
    {
        if (!channelFuture.valid())
        {
            return;
        }

        if (channelFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            return;
        }

        auto textureData = channelFuture.get();
        if (!textureData || textureData->mipData.empty())
        {
            pendingChannel = 0;
            return;
        }

        // Decompress if BC7
        if (textureData->compressionFormat == resource::TextureCompressionFormat::BC7)
        {
            if (!types::TextureCompressor::decompressAllMips(*textureData))
            {
                pendingChannel = 0;
                return;
            }
        }

        // Channel index: 1=R(0), 2=G(1), 3=B(2), 4=A(3)
        int channelIndex = pendingChannel - 1;

        const auto& srcMip = textureData->mipData[0];
        resource::TextureData channelData;
        channelData.width = textureData->width;
        channelData.height = textureData->height;
        channelData.numbersOfChannels = 4;
        channelData.compressionFormat = resource::TextureCompressionFormat::Uncompressed;

        resource::MipLevelData dstMip;
        dstMip.width = srcMip.width;
        dstMip.height = srcMip.height;
        dstMip.data.resize(srcMip.width * srcMip.height * 4);
        dstMip.dataSize = static_cast<uint32_t>(dstMip.data.size());

        for (uint32_t i = 0; i < srcMip.width * srcMip.height; ++i)
        {
            uint8_t value = srcMip.data[i * 4 + channelIndex];
            dstMip.data[i * 4 + 0] = value;
            dstMip.data[i * 4 + 1] = value;
            dstMip.data[i * 4 + 2] = value;
            dstMip.data[i * 4 + 3] = 255;
        }

        channelData.mipData.push_back(std::move(dstMip));
        channelData.mipLevels = 1;

        releaseChannelTexture();

        auto& dispatcher = events::EventDispatcher::instance();
        events::render::LoadEditorTextureFromDataCommand cmd;
        cmd.textureData = std::move(channelData);
        channelHandle = dispatcher.execute(cmd);
        pendingChannel = 0;
    }

    void ImagePreviewWindow::drawLoadingIndicator(float width, float height)
    {
        ImVec2 center(width * 0.5f, height * 0.5f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 spinnerCenter(windowPos.x + center.x, windowPos.y + center.y - 30.0f);

        float radius = 20.0f;
        float thickness = 4.0f;

        float time = static_cast<float>(ImGui::GetTime());
        int segments = 12;

        for (int i = 0; i < segments; i++)
        {
            float angle = (i / static_cast<float>(segments)) * 2.0f * 3.14159f;
            float alpha = std::fmod(time * 2.0f + i / static_cast<float>(segments), 1.0f);
            alpha = 0.2f + 0.8f * alpha;

            ImVec2 p1(spinnerCenter.x + std::cos(angle) * (radius - thickness),
                      spinnerCenter.y + std::sin(angle) * (radius - thickness));
            ImVec2 p2(spinnerCenter.x + std::cos(angle) * radius,
                      spinnerCenter.y + std::sin(angle) * radius);

            drawList->AddLine(p1, p2, IM_COL32(255, 255, 255, static_cast<int>(alpha * 255)), thickness);
        }

        ImGui::SetCursorPos(ImVec2(0, center.y + 10.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));

        std::string statusText = loadingProgress.statusMessage;
        ImVec2 textSize = ImGui::CalcTextSize(statusText.c_str());
        ImGui::SetCursorPosX((width - textSize.x) * 0.5f);
        ImGui::Text("%s", statusText.c_str());

        ImGui::SetCursorPosX((width - 200.0f) * 0.5f);
        ImGui::ProgressBar(loadingProgress.progress, ImVec2(200.0f, 20.0f), "");

        ImGui::PopStyleColor();

        if (loadingProgress.state == services::LoadingState::Error)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            std::string errorText = "Error: " + loadingProgress.errorMessage;
            textSize = ImGui::CalcTextSize(errorText.c_str());
            ImGui::SetCursorPosX((width - textSize.x) * 0.5f);
            ImGui::TextWrapped("%s", errorText.c_str());
            ImGui::PopStyleColor();
        }
    }

    void ImagePreviewWindow::drawImagePanel()
    {
        if (!imageHandle.isValid())
        {
            ImGui::TextDisabled("Failed to load image");
            return;
        }

        ImVec2 availSize = ImGui::GetContentRegionAvail();

        float imageAspect = static_cast<float>(imageHandle.width) / static_cast<float>(imageHandle.height);
        float availAspect = availSize.x / availSize.y;

        ImVec2 imageSize;
        if (imageAspect > availAspect)
        {
            imageSize.x = availSize.x * zoom;
            imageSize.y = imageSize.x / imageAspect;
        }
        else
        {
            imageSize.y = availSize.y * zoom;
            imageSize.x = imageSize.y * imageAspect;
        }

        void* displayDescriptor;
        if (channelView != 0 && channelHandle.isValid())
        {
            displayDescriptor = channelHandle.imguiDescriptorSet;
        }
        else
        {
            displayDescriptor = imageHandle.getMipDescriptor(static_cast<uint32_t>(selectedMipLevel));
        }

        ImGui::Image(displayDescriptor, imageSize);
    }

    void ImagePreviewWindow::drawInfoPanel()
    {
        ImGui::Text("Info");
        ImGui::Separator();

        if (imageHandle.isValid())
        {
            ImGui::Text("Width: %d", imageHandle.width);
            ImGui::Text("Height: %d", imageHandle.height);
            ImGui::Text("Type: %s", isHDR ? "HDR" : "LDR");
        }
        else
        {
            ImGui::TextDisabled("No image");
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (!isHDR && imageHandle.isValid() && imageHandle.mipLevels > 1)
        {
            if (ImGui::CollapsingHeader("Mip Levels", ImGuiTreeNodeFlags_DefaultOpen))
            {
                float itemWidth = ImGui::GetContentRegionAvail().x;

                std::vector<std::string> mipLabels;
                mipLabels.reserve(imageHandle.mipLevels);

                uint32_t mipWidth = imageHandle.width;
                uint32_t mipHeight = imageHandle.height;

                for (uint32_t i = 0; i < imageHandle.mipLevels; ++i)
                {
                    mipLabels.push_back("Mip " + std::to_string(i) + " (" +
                        std::to_string(mipWidth) + "x" +
                        std::to_string(mipHeight) + ")");
                    mipWidth = std::max(1u, mipWidth / 2);
                    mipHeight = std::max(1u, mipHeight / 2);
                }

                ImGui::SetNextItemWidth(itemWidth);
                if (ImGui::BeginCombo("##MipLevel", mipLabels[selectedMipLevel].c_str()))
                {
                    for (int i = 0; i < static_cast<int>(imageHandle.mipLevels); ++i)
                    {
                        bool isSelected = (selectedMipLevel == i);
                        if (ImGui::Selectable(mipLabels[i].c_str(), isSelected))
                        {
                            selectedMipLevel = i;
                        }
                        if (isSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                uint32_t selWidth = imageHandle.width >> selectedMipLevel;
                uint32_t selHeight = imageHandle.height >> selectedMipLevel;
                selWidth = std::max(1u, selWidth);
                selHeight = std::max(1u, selHeight);

                ImGui::TextDisabled("Selected: %dx%d", selWidth, selHeight);

                float reductionPercent = 100.0f / static_cast<float>(1 << (selectedMipLevel * 2));
                ImGui::TextDisabled("Size: %.1f%% of original", reductionPercent);
            }

            ImGui::Separator();
            ImGui::Spacing();
        }

        if (imageHandle.isValid() && ImGui::CollapsingHeader("Channels", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float buttonWidth = ImGui::GetContentRegionAvail().x;
            float spacing = ImGui::GetStyle().ItemSpacing.x;
            float btnW = (buttonWidth - spacing * 2) / 3.0f;

            auto channelButton = [&](const char* label, int channel, const ImVec4& color, float width)
            {
                if (channelView == channel)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, color);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
                }
                else
                {
                    ImVec4 dim(color.x * 0.4f, color.y * 0.4f, color.z * 0.4f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button, dim);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x * 0.6f, color.y * 0.6f, color.z * 0.6f, 1.0f));
                }

                if (ImGui::Button(label, ImVec2(width, 0)))
                {
                    int newChannel = (channelView == channel) ? 0 : channel;
                    if (newChannel != channelView)
                    {
                        channelView = newChannel;
                        if (channelView == 0)
                        {
                            releaseChannelTexture();
                        }
                        else
                        {
                            startChannelBuild(channelView);
                        }
                    }
                }
                ImGui::PopStyleColor(2);
            };

            channelButton("R", 1, ImVec4(0.8f, 0.2f, 0.2f, 1.0f), btnW);
            ImGui::SameLine();
            channelButton("G", 2, ImVec4(0.2f, 0.8f, 0.2f, 1.0f), btnW);
            ImGui::SameLine();
            channelButton("B", 3, ImVec4(0.2f, 0.2f, 0.8f, 1.0f), btnW);

            float btnW2 = (buttonWidth - spacing) / 2.0f;
            channelButton("A", 4, ImVec4(0.6f, 0.6f, 0.6f, 1.0f), btnW2);
            ImGui::SameLine();
            channelButton("RGB", 0, ImVec4(0.5f, 0.5f, 0.5f, 1.0f), btnW2);

            if (pendingChannel != 0)
            {
                ImGui::TextDisabled("Loading channel...");
            }
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float itemWidth = ImGui::GetContentRegionAvail().x - 50.0f;

            ImGui::Text("Zoom");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::SliderFloat("##Zoom", &zoom, 0.1f, 10.0f, "%.1fx");

            if (ImGui::Button("+", ImVec2(itemWidth / 2 - 2, 0)))
            {
                zoom = glm::clamp(zoom * 1.2f, 0.1f, 10.0f);
            }
            ImGui::SameLine();
            if (ImGui::Button("-", ImVec2(itemWidth / 2 - 2, 0)))
            {
                zoom = glm::clamp(zoom / 1.2f, 0.1f, 10.0f);
            }

            ImGui::Spacing();

            if (ImGui::Button("Reset View", ImVec2(-1, 0)))
            {
                zoom = 1.0f;
            }
        }
    }

    void ImagePreviewWindow::startChannelBuild(int channel)
    {
        // Wait for any previous pending load before starting a new one
        if (channelFuture.valid())
        {
            channelFuture.wait();
        }

        pendingChannel = channel;
        channelFuture = resource::ResourceManager::loadTextureAsync(asset::AssetRef::fromPath(imagePath));
    }

    void ImagePreviewWindow::releaseChannelTexture()
    {
        if (channelHandle.isValid())
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::render::ReleaseEditorTextureCommand releaseCmd;
            releaseCmd.handle = channelHandle.imguiDescriptorSet;
            dispatcher.execute(releaseCmd);
            channelHandle = {};
        }
    }
}
