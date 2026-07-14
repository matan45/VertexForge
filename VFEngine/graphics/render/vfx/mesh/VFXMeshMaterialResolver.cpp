#include "VFXMeshMaterialResolver.hpp"
#include "../../material/MaterialPBRExtractor.hpp"

namespace render::vfx
{
    ResolvedVFXMeshMaterial resolveVFXMeshMaterial(const mesh::ExtractedPBRValues* pbr)
    {
        ResolvedVFXMeshMaterial out;

        // No material assigned, or extraction failed (a valid extraction sets a non-empty materialPath).
        // Leave the struct defaults => hasMaterial stays false => shader uses the legacy single-.vfImage path.
        if (!pbr || pbr->materialPath.empty())
        {
            return out;
        }

        out.hasMaterial = true;
        out.albedoPath = pbr->albedoTexturePath;
        out.normalPath = pbr->normalTexturePath;
        out.ormPath = pbr->ormTexturePath;
        out.emissivePath = pbr->emissionTexturePath;

        out.metallic = pbr->metallic;
        out.roughness = pbr->roughness;
        out.ao = pbr->ao;
        out.emissionStrength = pbr->emission;
        out.albedoTint = pbr->albedo;

        out.usesORM = pbr->usesORM();
        // VFX packs metallic/roughness/AO into a single ORM map. A material authored with separate
        // metallic/roughness/AO maps can't be represented; fall back to the ORM/scalar path and warn.
        out.warnSeparateOrmMaps = !out.usesORM &&
            (!pbr->metallicTexturePath.empty() ||
             !pbr->roughnessTexturePath.empty() ||
             !pbr->aoTexturePath.empty());

        return out;
    }
}
