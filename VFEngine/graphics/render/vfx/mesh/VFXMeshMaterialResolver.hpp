#pragma once

#include <glm/glm.hpp>
#include <string>

namespace render::mesh
{
    struct ExtractedPBRValues;
}

namespace render::vfx
{
    // VK-1526: the VFX-mesh-particle-supported PBR fields resolved from a referenced material's
    // extracted PBR values. Texture members are resolved file paths (empty => no texture, use the
    // scalar/default), consumed by the bindless registration path in VFXMeshGPUPipeline. Mirrors the
    // VK-1486 terrain precedent (ResolvedTerrainLayerPBR): mesh particles source their PBR texture set
    // from a .vfMat/.vfMatInstance, using the representable subset (4-map ORM-packed + scalars + tint).
    struct ResolvedVFXMeshMaterial
    {
        std::string albedoPath;
        std::string normalPath;
        std::string ormPath;      // packed ORM (R=AO, G=Roughness, B=Metallic)
        std::string emissivePath;

        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emissionStrength = 0.0f;
        glm::vec4 albedoTint{1.0f, 1.0f, 1.0f, 1.0f}; // multiplied with the particle color

        bool hasMaterial = false;         // false => byte-identical legacy single-.vfImage path
        bool usesORM = false;             // an ORM map is present (else metallic/roughness/ao are scalar)
        bool warnSeparateOrmMaps = false; // material carries separate metallic/roughness/AO maps VFX can't pack
    };

    // Resolves the VFX-mesh-supported PBR fields from a material's extracted PBR values. Semantics:
    //   - pbr == nullptr, or extraction failed (pbr->materialPath empty) => hasMaterial = false,
    //     empty paths + default scalars (shader takes the legacy single-.vfImage path).
    //   - otherwise => copy albedo/normal/ORM/emissive texture paths + metallic/roughness/ao/emission
    //     scalars + albedo tint (the representable subset). usesORM from the ORM map presence;
    //     warnSeparateOrmMaps when the material uses separate metallic/roughness/AO maps instead of a
    //     packed ORM (VFX packs ORM only — warn-once at the call site, like VK-1486 terrain).
    ResolvedVFXMeshMaterial resolveVFXMeshMaterial(const mesh::ExtractedPBRValues* pbr);
}
