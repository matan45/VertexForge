#pragma once
#include "Types.hpp"
#include <glm/glm.hpp>
#include <meshoptimizer.h>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace resource
{
    enum class MeshCompressionFlags : uint32_t
    {
        NONE                 = 0,
        QUANTIZED_VERTICES   = 1 << 0,
        MESHOPT_VERTEX_CODEC = 1 << 1,
        MESHOPT_INDEX_CODEC  = 1 << 2,
        ALL = QUANTIZED_VERTICES | MESHOPT_VERTEX_CODEC | MESHOPT_INDEX_CODEC
    };

    inline MeshCompressionFlags operator|(MeshCompressionFlags a, MeshCompressionFlags b)
    {
        return static_cast<MeshCompressionFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline bool hasFlag(MeshCompressionFlags flags, MeshCompressionFlags flag)
    {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
    }

    namespace quantization
    {
#pragma pack(push, 1)
        struct CompressedVertex
        {
            uint16_t px, py, pz;              // 6B  - position relative to AABB [0, 65535]
            int16_t  nx, ny;                  // 4B  - octahedral-encoded normal (snorm16)
            uint16_t tu, tv;                  // 4B  - float16 UVs
            uint16_t bi0, bi1, bi2, bi3;     // 8B  - bone indices (uint16)
            uint8_t  bw0, bw1, bw2, bw3;     // 4B  - normalized bone weights (unorm8)
            uint16_t _pad = 0;                // 2B  - alignment padding (meshopt needs stride % 4 == 0)
        };
#pragma pack(pop)
        static_assert(sizeof(CompressedVertex) == 28, "CompressedVertex must be 28 bytes");
        static_assert(sizeof(CompressedVertex) % 4 == 0, "CompressedVertex stride must be multiple of 4");

        struct QuantizationAABB
        {
            glm::vec3 min{0.0f};
            glm::vec3 max{0.0f};
        };

        // ---- AABB computation ----

        inline QuantizationAABB computeAABB(const std::vector<Vertex>& vertices)
        {
            QuantizationAABB aabb;
            if (vertices.empty()) return aabb;

            aabb.min = vertices[0].position;
            aabb.max = vertices[0].position;

            for (const auto& v : vertices)
            {
                aabb.min = glm::min(aabb.min, v.position);
                aabb.max = glm::max(aabb.max, v.position);
            }

            return aabb;
        }

        // ---- Octahedral normal encoding ----

        inline void octEncode(const glm::vec3& n, int16_t& outX, int16_t& outY)
        {
            float l1 = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
            if (l1 < 1e-10f)
            {
                outX = 0;
                outY = 0;
                return;
            }
            float invL1 = 1.0f / l1;
            float ox = n.x * invL1;
            float oy = n.y * invL1;

            if (n.z < 0.0f)
            {
                float tmpX = (1.0f - std::abs(oy)) * (ox >= 0.0f ? 1.0f : -1.0f);
                float tmpY = (1.0f - std::abs(ox)) * (oy >= 0.0f ? 1.0f : -1.0f);
                ox = tmpX;
                oy = tmpY;
            }

            outX = static_cast<int16_t>(meshopt_quantizeSnorm(ox, 16));
            outY = static_cast<int16_t>(meshopt_quantizeSnorm(oy, 16));
        }

        inline glm::vec3 octDecode(int16_t encX, int16_t encY)
        {
            float ox = encX / 32767.0f;
            float oy = encY / 32767.0f;
            float oz = 1.0f - std::abs(ox) - std::abs(oy);

            if (oz < 0.0f)
            {
                float tmpX = (1.0f - std::abs(oy)) * (ox >= 0.0f ? 1.0f : -1.0f);
                float tmpY = (1.0f - std::abs(ox)) * (oy >= 0.0f ? 1.0f : -1.0f);
                ox = tmpX;
                oy = tmpY;
            }

            return glm::normalize(glm::vec3(ox, oy, oz));
        }

        // ---- Single vertex quantize/dequantize ----

        inline CompressedVertex quantizeVertex(const Vertex& v, const QuantizationAABB& aabb)
        {
            CompressedVertex cv{};
            constexpr float epsilon = 1e-7f;

            // Position: map [min, max] → [0, 65535]
            glm::vec3 extent = aabb.max - aabb.min;
            for (int i = 0; i < 3; ++i)
            {
                if (extent[i] < epsilon) extent[i] = 1.0f;
            }

            cv.px = static_cast<uint16_t>(std::clamp((v.position.x - aabb.min.x) / extent.x * 65535.0f, 0.0f, 65535.0f));
            cv.py = static_cast<uint16_t>(std::clamp((v.position.y - aabb.min.y) / extent.y * 65535.0f, 0.0f, 65535.0f));
            cv.pz = static_cast<uint16_t>(std::clamp((v.position.z - aabb.min.z) / extent.z * 65535.0f, 0.0f, 65535.0f));

            // Normal: octahedral encoding
            octEncode(v.normal, cv.nx, cv.ny);

            // UVs: float16
            cv.tu = meshopt_quantizeHalf(v.texCoords.x);
            cv.tv = meshopt_quantizeHalf(v.texCoords.y);

            // Bone indices: int32 → uint16 (-1 → 0xFFFF)
            cv.bi0 = static_cast<uint16_t>(v.boneIndices.x < 0 ? 0xFFFF : v.boneIndices.x);
            cv.bi1 = static_cast<uint16_t>(v.boneIndices.y < 0 ? 0xFFFF : v.boneIndices.y);
            cv.bi2 = static_cast<uint16_t>(v.boneIndices.z < 0 ? 0xFFFF : v.boneIndices.z);
            cv.bi3 = static_cast<uint16_t>(v.boneIndices.w < 0 ? 0xFFFF : v.boneIndices.w);

            // Bone weights: float → uint8 (normalized to [0, 255])
            cv.bw0 = static_cast<uint8_t>(std::clamp(v.boneWeights.x * 255.0f + 0.5f, 0.0f, 255.0f));
            cv.bw1 = static_cast<uint8_t>(std::clamp(v.boneWeights.y * 255.0f + 0.5f, 0.0f, 255.0f));
            cv.bw2 = static_cast<uint8_t>(std::clamp(v.boneWeights.z * 255.0f + 0.5f, 0.0f, 255.0f));
            cv.bw3 = static_cast<uint8_t>(std::clamp(v.boneWeights.w * 255.0f + 0.5f, 0.0f, 255.0f));

            cv._pad = 0;
            return cv;
        }

        inline Vertex dequantizeVertex(const CompressedVertex& cv, const QuantizationAABB& aabb)
        {
            Vertex v{};
            constexpr float epsilon = 1e-7f;

            glm::vec3 extent = aabb.max - aabb.min;
            for (int i = 0; i < 3; ++i)
            {
                if (extent[i] < epsilon) extent[i] = 1.0f;
            }

            // Position
            v.position.x = aabb.min.x + (cv.px / 65535.0f) * extent.x;
            v.position.y = aabb.min.y + (cv.py / 65535.0f) * extent.y;
            v.position.z = aabb.min.z + (cv.pz / 65535.0f) * extent.z;

            // Normal
            v.normal = octDecode(cv.nx, cv.ny);

            // UVs
            v.texCoords.x = meshopt_dequantizeHalf(cv.tu);
            v.texCoords.y = meshopt_dequantizeHalf(cv.tv);

            // Bone indices: uint16 → int32 (0xFFFF → -1)
            v.boneIndices.x = (cv.bi0 == 0xFFFF) ? -1 : static_cast<int32_t>(cv.bi0);
            v.boneIndices.y = (cv.bi1 == 0xFFFF) ? -1 : static_cast<int32_t>(cv.bi1);
            v.boneIndices.z = (cv.bi2 == 0xFFFF) ? -1 : static_cast<int32_t>(cv.bi2);
            v.boneIndices.w = (cv.bi3 == 0xFFFF) ? -1 : static_cast<int32_t>(cv.bi3);

            // Bone weights: uint8 → float, re-normalize
            v.boneWeights.x = cv.bw0 / 255.0f;
            v.boneWeights.y = cv.bw1 / 255.0f;
            v.boneWeights.z = cv.bw2 / 255.0f;
            v.boneWeights.w = cv.bw3 / 255.0f;

            float totalWeight = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
            if (totalWeight > 0.0f)
            {
                v.boneWeights /= totalWeight;
            }

            return v;
        }

        // ---- Batch operations ----

        inline std::vector<CompressedVertex> quantizeVertices(const std::vector<Vertex>& vertices,
                                                               const QuantizationAABB& aabb)
        {
            std::vector<CompressedVertex> result(vertices.size());
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                result[i] = quantizeVertex(vertices[i], aabb);
            }
            return result;
        }

        inline void dequantizeVertices(const CompressedVertex* compressed, size_t count,
                                        const QuantizationAABB& aabb,
                                        std::vector<Vertex>& outVertices)
        {
            outVertices.resize(count);
            for (size_t i = 0; i < count; ++i)
            {
                outVertices[i] = dequantizeVertex(compressed[i], aabb);
            }
        }

    } // namespace quantization
} // namespace resource
