#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details
{
    class MeshDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        void drawMeshPath(const std::string& meshPath);
        void drawSelectMeshButton(services::EntityHandle handle, const services::MeshData& currentData);
        void drawBoundingBoxCheckbox(services::EntityHandle handle, const services::MeshData& currentData);
    };
}
