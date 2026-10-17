#pragma once
#include "../AssetImporter.hpp"
#include "../../types/Mesh.hpp"
#include "../../types/Animation.hpp"

namespace import::builtin
{
    // OBJ / FBX / DAE / GLTF / GLB -> .vfMesh (+ .vfAnim for embedded animations).
    // Every written file is reported in context.outputFiles with its own asset type
    // (meshes, extracted textures, one <file>_<take>.vfAnim per animation).
    // With the animationOnly option the mesh pass is skipped entirely and only
    // .vfAnim is produced (retargeting workflows import animation-only FBX
    // files without emitting a junk mesh); such an import fails when no clip
    // could be written.
    class MeshImporter : public AssetImporter
    {
    public:
        std::vector<FormatInfo> formats() const override;
        bool matches(const std::string& fileType, const DetectionInput& input) const override;
        void process(pipeline::ImportContext& context) override;
        std::vector<ImportOptionDesc> options() const override;

    private:
        types::Mesh meshProcessor;
        types::Animation animationProcessor;
    };
}
