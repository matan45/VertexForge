#pragma once
#include "../AssetImporter.hpp"
#include "../../types/Mesh.hpp"
#include "../../types/Animation.hpp"

namespace import::builtin
{
    // OBJ / FBX / DAE / GLTF / GLB -> .vfMesh (+ .vfAnim for embedded animations)
    class MeshImporter : public AssetImporter
    {
    public:
        std::vector<FormatInfo> formats() const override;
        bool matches(const std::string& fileType, const DetectionInput& input) const override;
        void process(pipeline::ImportContext& context) override;

    private:
        types::Mesh meshProcessor;
        types::Animation animationProcessor;
    };
}
