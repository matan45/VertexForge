#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "RenderTexturePickerWidget.hpp"
#include <unordered_map>
#include <string>
#include <vector>

namespace windows::details
{
    class MaterialDrawer
    {
    private:
        std::unordered_map<std::string, std::vector<std::string>> submeshNameCache;
        // VK-1418: one picker per RTT-drivable slot (albedo, emission).
        RenderTexturePickerWidget albedoRttPicker;
        RenderTexturePickerWidget emissionRttPicker;

    public:
        void draw(services::EntityHandle handle);
        void clearCache();

    private:
        bool drawHeader();
        void drawDefaultMaterial(services::EntityHandle handle, const services::MaterialData& matData);
        void drawSubmeshMaterials(services::EntityHandle handle,
                                  const std::string& meshPath,
                                  const services::MaterialData& matData);
        void drawSubmeshEntry(services::EntityHandle handle,
                              const std::string& submeshName,
                              const std::string& currentMat,
                              int index);
        // VK-1418: per-slot RTT picker (albedo/emission). Commits the full MaterialData DTO.
        void drawRenderTextureSlots(services::EntityHandle handle, const services::MaterialData& matData);
        bool drawRenderTextureSlot(RenderTexturePickerWidget& picker,
                                   const char* slotKey,
                                   const char* comboLabel,
                                   services::MaterialData& data);
        const std::vector<std::string>& getSubmeshNames(const std::string& meshPath);
    };
}
