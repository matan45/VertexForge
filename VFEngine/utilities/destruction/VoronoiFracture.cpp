#include "VoronoiFracture.hpp"
#include "MeshClipper.hpp"
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace destruction
{
    float VoronoiFracture::signedTetraVolume(
        const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        return glm::dot(a, glm::cross(b, c)) / 6.0f;
    }

    bool VoronoiFracture::generateFragments(
        std::vector<FragmentData>& outFragments,
        const resource::LODLevel& lod0,
        const std::vector<glm::vec3>& seeds,
        const FractureConfig& config,
        FractureProgressCallback& progressCallback,
        std::atomic<bool>* cancelFlag)
    {
        float progressPerCell = 0.85f / static_cast<float>(seeds.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(seeds.size()); ++i)
        {
            if (cancelFlag && cancelFlag->load(std::memory_order_relaxed))
                return false;

            auto fragment = buildFragment(lod0.vertices, lod0.indices, seeds, i, config);
            if (!fragment.mesh.lodLevels.empty() &&
                !fragment.mesh.lodLevels[0].vertices.empty() &&
                fragment.mesh.lodLevels[0].indices.size() >= 3)
            {
                outFragments.push_back(std::move(fragment));
            }
            if (progressCallback)
                progressCallback(0.05f + progressPerCell * (i + 1), "Fracturing");
        }
        return true;
    }

    FractureResult VoronoiFracture::fracture(
        const resource::MeshData& inputMesh,
        const FractureConfig& config,
        FractureProgressCallback progressCallback,
        std::atomic<bool>* cancelFlag)
    {
        FractureResult result;

        if (inputMesh.lodLevels.empty() || inputMesh.lodLevels[0].vertices.empty())
        {
            result.errorMessage = "Input mesh has no geometry";
            return result;
        }
        if (config.cellCount < 2)
        {
            result.errorMessage = "Cell count must be at least 2";
            return result;
        }

        const auto& lod0 = inputMesh.lodLevels[0];
        glm::vec3 bboxMin, bboxMax;
        computeBoundingBox(lod0.vertices, bboxMin, bboxMax);

        glm::vec3 bboxPadding = (bboxMax - bboxMin) * 0.01f;
        auto seeds = generateSeeds(bboxMin + bboxPadding, bboxMax - bboxPadding, config);
        if (progressCallback) progressCallback(0.05f, "Seeds generated");

        if (!generateFragments(result.fragments, lod0, seeds, config, progressCallback, cancelFlag))
        {
            result.errorMessage = "Cancelled";
            return result;
        }
        if (result.fragments.empty())
        {
            result.errorMessage = "No fragments generated";
            return result;
        }

        buildConnectivityGraph(result.fragments, seeds);
        if (progressCallback) progressCallback(0.95f, "Connectivity built");

        for (size_t i = 0; i < result.fragments.size(); ++i)
            result.fragments[i].mesh.name = "fragment_" + std::to_string(i);

        result.success = true;
        if (progressCallback) progressCallback(1.0f, "Complete");
        return result;
    }

    resource::MeshesData VoronoiFracture::toMeshesData(const FractureResult& result)
    {
        resource::MeshesData meshesData;
        meshesData.headerFileType = resource::FileType::MESH;
        meshesData.numberOfMeshes = static_cast<uint32_t>(result.fragments.size());
        meshesData.hasSkinning = false;

        for (const auto& fragment : result.fragments)
        {
            resource::MeshData mesh = fragment.mesh;

            // Re-center vertices around centerOfMass so each fragment
            // has its origin at its own center (like Unity/Unreal)
            for (auto& lod : mesh.lodLevels)
            {
                for (auto& vertex : lod.vertices)
                {
                    vertex.position -= fragment.centerOfMass;
                }
            }

            meshesData.meshes.push_back(std::move(mesh));
        }

        return meshesData;
    }

    std::vector<glm::vec3> VoronoiFracture::generateSeeds(
        const glm::vec3& bboxMin,
        const glm::vec3& bboxMax,
        const FractureConfig& config)
    {
        std::mt19937 rng(config.randomSeed);

        switch (config.seedDistribution)
        {
        case SeedDistribution::Uniform:
            return generateUniformSeeds(bboxMin, bboxMax, config, rng);
        case SeedDistribution::Clustered:
            return generateClusteredSeeds(bboxMin, bboxMax, config, rng);
        case SeedDistribution::ArtistPlaced:
            return config.artistSeeds;
        }
        return {};
    }

    std::vector<glm::vec3> VoronoiFracture::generateUniformSeeds(
        const glm::vec3& bboxMin,
        const glm::vec3& bboxMax,
        const FractureConfig& config,
        std::mt19937& rng)
    {
        std::vector<glm::vec3> seeds;
        glm::vec3 size = bboxMax - bboxMin;
        float cellSize = std::cbrt(size.x * size.y * size.z / static_cast<float>(config.cellCount));

        uint32_t nx = std::max(1u, static_cast<uint32_t>(std::ceil(size.x / cellSize)));
        uint32_t ny = std::max(1u, static_cast<uint32_t>(std::ceil(size.y / cellSize)));
        uint32_t nz = std::max(1u, static_cast<uint32_t>(std::ceil(size.z / cellSize)));

        float dx = size.x / static_cast<float>(nx);
        float dy = size.y / static_cast<float>(ny);
        float dz = size.z / static_cast<float>(nz);

        std::uniform_real_distribution<float> jitterX(-dx * 0.4f, dx * 0.4f);
        std::uniform_real_distribution<float> jitterY(-dy * 0.4f, dy * 0.4f);
        std::uniform_real_distribution<float> jitterZ(-dz * 0.4f, dz * 0.4f);

        for (uint32_t z = 0; z < nz && seeds.size() < config.cellCount; ++z)
            for (uint32_t y = 0; y < ny && seeds.size() < config.cellCount; ++y)
                for (uint32_t x = 0; x < nx && seeds.size() < config.cellCount; ++x)
                {
                    glm::vec3 center = bboxMin + glm::vec3(
                        (x + 0.5f) * dx + jitterX(rng),
                        (y + 0.5f) * dy + jitterY(rng),
                        (z + 0.5f) * dz + jitterZ(rng));
                    seeds.push_back(glm::clamp(center, bboxMin, bboxMax));
                }

        std::uniform_real_distribution<float> distX(bboxMin.x, bboxMax.x);
        std::uniform_real_distribution<float> distY(bboxMin.y, bboxMax.y);
        std::uniform_real_distribution<float> distZ(bboxMin.z, bboxMax.z);
        while (seeds.size() < config.cellCount)
            seeds.emplace_back(distX(rng), distY(rng), distZ(rng));

        return seeds;
    }

    std::vector<glm::vec3> VoronoiFracture::generateClusteredSeeds(
        const glm::vec3& bboxMin,
        const glm::vec3& bboxMax,
        const FractureConfig& config,
        std::mt19937& rng)
    {
        std::vector<glm::vec3> seeds;
        glm::vec3 size = bboxMax - bboxMin;
        float maxRadius = glm::length(size) * config.clusterParams.clusterRadius;

        std::uniform_real_distribution<float> distX(bboxMin.x, bboxMax.x);
        std::uniform_real_distribution<float> distY(bboxMin.y, bboxMax.y);
        std::uniform_real_distribution<float> distZ(bboxMin.z, bboxMax.z);
        std::normal_distribution<float> gaussian(0.0f, maxRadius * 0.33f);

        uint32_t clusterCount = std::min(config.clusterParams.clusterCount, config.cellCount);
        std::vector<glm::vec3> clusterCenters;
        for (uint32_t c = 0; c < clusterCount; ++c)
            clusterCenters.emplace_back(distX(rng), distY(rng), distZ(rng));

        uint32_t seedsPerCluster = config.cellCount / clusterCount;
        uint32_t remainder = config.cellCount % clusterCount;

        for (uint32_t c = 0; c < clusterCount; ++c)
        {
            uint32_t count = seedsPerCluster + (c < remainder ? 1 : 0);
            for (uint32_t s = 0; s < count; ++s)
            {
                glm::vec3 offset(gaussian(rng), gaussian(rng), gaussian(rng));
                seeds.push_back(glm::clamp(clusterCenters[c] + offset, bboxMin, bboxMax));
            }
        }
        return seeds;
    }

    void VoronoiFracture::clipMeshToVoronoiCell(
        std::vector<resource::Vertex>& currentVertices,
        std::vector<uint32_t>& currentIndices,
        std::vector<PlaneClipInfo>& clipInfos,
        const std::vector<glm::vec3>& seeds,
        uint32_t cellIndex,
        float innerUVScale)
    {
        const glm::vec3& cellSeed = seeds[cellIndex];

        std::vector<uint32_t> sortedSeeds;
        sortedSeeds.reserve(seeds.size() - 1);
        for (uint32_t j = 0; j < static_cast<uint32_t>(seeds.size()); ++j)
            if (j != cellIndex) sortedSeeds.push_back(j);

        std::sort(sortedSeeds.begin(), sortedSeeds.end(),
            [&](uint32_t a, uint32_t b) {
                return glm::distance(cellSeed, seeds[a]) < glm::distance(cellSeed, seeds[b]);
            });

        for (uint32_t j : sortedSeeds)
        {
            if (currentVertices.empty() || currentIndices.size() < 3) break;

            glm::vec3 midpoint = (cellSeed + seeds[j]) * 0.5f;
            glm::vec3 normal = glm::normalize(cellSeed - seeds[j]);
            glm::vec4 plane(normal, -glm::dot(normal, midpoint));

            auto clipResult = MeshClipper::clipToHalfSpace(currentVertices, currentIndices, plane);
            if (!clipResult.didClip) continue;

            if (clipResult.vertices.empty() || clipResult.indices.size() < 3)
            {
                currentVertices.clear();
                currentIndices.clear();
                break;
            }

            // Cap the cut face NOW while vertex indices are still valid
            currentVertices = std::move(clipResult.vertices);
            currentIndices = std::move(clipResult.indices);

            if (!clipResult.cutEdges.empty())
            {
                capCutFace(currentVertices, currentIndices,
                           clipResult.cutEdges, plane, innerUVScale);
            }
        }
    }

    FragmentData VoronoiFracture::buildFragment(
        const std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        const std::vector<glm::vec3>& seeds,
        uint32_t cellIndex,
        const FractureConfig& config)
    {
        FragmentData fragment;
        fragment.cellIndex = cellIndex;

        std::vector<resource::Vertex> currentVertices = vertices;
        std::vector<uint32_t> currentIndices = indices;
        std::vector<PlaneClipInfo> clipInfos;

        clipMeshToVoronoiCell(currentVertices, currentIndices, clipInfos, seeds, cellIndex, config.innerUVScale);

        if (currentVertices.empty() || currentIndices.size() < 3)
            return fragment;

        resource::LODLevel lod0;
        lod0.vertices = std::move(currentVertices);
        lod0.indices = std::move(currentIndices);
        fragment.mesh.lodLevels.push_back(std::move(lod0));

        const auto& finalVerts = fragment.mesh.lodLevels[0].vertices;
        const auto& finalIndices = fragment.mesh.lodLevels[0].indices;
        fragment.centerOfMass = computeCenterOfMass(finalVerts, finalIndices);
        fragment.volume = computeVolume(finalVerts, finalIndices);

        return fragment;
    }

    bool VoronoiFracture::isVoronoiNeighbor(
        const std::vector<glm::vec3>& seeds,
        uint32_t cellI,
        uint32_t cellJ)
    {
        glm::vec3 midpoint = (seeds[cellI] + seeds[cellJ]) * 0.5f;
        float distIJ = glm::distance(seeds[cellI], seeds[cellJ]);

        for (uint32_t k = 0; k < static_cast<uint32_t>(seeds.size()); ++k)
        {
            if (k == cellI || k == cellJ) continue;
            if (glm::distance(seeds[k], midpoint) < distIJ * 0.5f - 1e-5f)
                return false;
        }
        return true;
    }

    void VoronoiFracture::buildConnectivityGraph(
        std::vector<FragmentData>& fragments,
        const std::vector<glm::vec3>& seeds)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(fragments.size()); ++i)
        {
            for (uint32_t j = i + 1; j < static_cast<uint32_t>(fragments.size()); ++j)
            {
                if (!isVoronoiNeighbor(seeds, fragments[i].cellIndex, fragments[j].cellIndex))
                    continue;

                glm::vec3 minI, maxI, minJ, maxJ;
                computeBoundingBox(fragments[i].mesh.lodLevels[0].vertices, minI, maxI);
                computeBoundingBox(fragments[j].mesh.lodLevels[0].vertices, minJ, maxJ);

                float avgSize = (glm::length(maxI - minI) + glm::length(maxJ - minJ)) * 0.5f;
                float sharedArea = avgSize * avgSize * 0.25f;

                fragments[i].neighbors.push_back({j, sharedArea});
                fragments[j].neighbors.push_back({i, sharedArea});
            }
        }
    }

    glm::vec3 VoronoiFracture::computeCenterOfMass(
        const std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices)
    {
        if (indices.size() < 3)
        {
            glm::vec3 sum(0.0f);
            for (const auto& v : vertices) sum += v.position;
            return vertices.empty() ? glm::vec3(0.0f) : sum / static_cast<float>(vertices.size());
        }

        glm::vec3 weightedSum(0.0f);
        float totalVolume = 0.0f;

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const glm::vec3& a = vertices[indices[i]].position;
            const glm::vec3& b = vertices[indices[i + 1]].position;
            const glm::vec3& c = vertices[indices[i + 2]].position;

            float tetVol = signedTetraVolume(a, b, c);
            weightedSum += (a + b + c) * 0.25f * tetVol;
            totalVolume += tetVol;
        }

        if (std::abs(totalVolume) < 1e-10f)
        {
            glm::vec3 sum(0.0f);
            for (const auto& v : vertices) sum += v.position;
            return sum / static_cast<float>(vertices.size());
        }
        return weightedSum / totalVolume;
    }

    float VoronoiFracture::computeVolume(
        const std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices)
    {
        float volume = 0.0f;
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            volume += signedTetraVolume(
                vertices[indices[i]].position,
                vertices[indices[i + 1]].position,
                vertices[indices[i + 2]].position);
        }
        return std::abs(volume);
    }

    std::vector<std::vector<uint32_t>> VoronoiFracture::buildEdgeLoops(
        const std::vector<std::pair<uint32_t, uint32_t>>& cutEdges)
    {
        std::unordered_map<uint32_t, std::vector<uint32_t>> adjacency;
        for (const auto& [a, b] : cutEdges)
        {
            adjacency[a].push_back(b);
            adjacency[b].push_back(a);
        }

        std::unordered_set<uint32_t> visited;
        std::vector<std::vector<uint32_t>> loops;

        for (const auto& [startVertex, _] : adjacency)
        {
            if (visited.count(startVertex)) continue;

            std::vector<uint32_t> loop;
            uint32_t current = startVertex;
            uint32_t prev = UINT32_MAX;

            uint32_t maxSteps = static_cast<uint32_t>(adjacency.size()) + 1;
            uint32_t steps = 0;
            while (steps++ < maxSteps)
            {
                if (visited.count(current) && !loop.empty()) break;
                visited.insert(current);
                loop.push_back(current);

                auto it = adjacency.find(current);
                if (it == adjacency.end()) break;
                const auto& neighbors = it->second;
                uint32_t next = UINT32_MAX;
                for (uint32_t n : neighbors)
                    if (n != prev) { next = n; break; }

                if (next == UINT32_MAX) break;
                prev = current;
                current = next;
            }

            if (loop.size() >= 3)
                loops.push_back(std::move(loop));
        }
        return loops;
    }

    void VoronoiFracture::capCutFace(
        std::vector<resource::Vertex>& vertices,
        std::vector<uint32_t>& indices,
        const std::vector<std::pair<uint32_t, uint32_t>>& cutEdges,
        const glm::vec4& plane,
        float uvScale)
    {
        if (cutEdges.empty()) return;

        auto loops = buildEdgeLoops(cutEdges);

        glm::vec3 planeNormal = glm::vec3(plane);
        glm::vec3 tangent = (std::abs(planeNormal.y) < 0.99f)
            ? glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), planeNormal))
            : glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), planeNormal));
        glm::vec3 bitangent = glm::cross(planeNormal, tangent);
        glm::vec3 capNormal = -planeNormal;

        for (const auto& loop : loops)
        {
            uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

            for (uint32_t idx : loop)
            {
                resource::Vertex capVertex;
                capVertex.position = vertices[idx].position;
                capVertex.normal = capNormal;
                capVertex.texCoords.x = glm::dot(capVertex.position, tangent) * uvScale;
                capVertex.texCoords.y = glm::dot(capVertex.position, bitangent) * uvScale;
                capVertex.boneIndices = glm::ivec4(-1);
                capVertex.boneWeights = glm::vec4(0.0f);
                vertices.push_back(capVertex);
            }

            for (uint32_t i = 1; i + 1 < static_cast<uint32_t>(loop.size()); ++i)
            {
                indices.push_back(baseIndex);
                indices.push_back(baseIndex + i);
                indices.push_back(baseIndex + i + 1);
            }
        }
    }

    void VoronoiFracture::computeBoundingBox(
        const std::vector<resource::Vertex>& vertices,
        glm::vec3& outMin,
        glm::vec3& outMax)
    {
        if (vertices.empty())
        {
            outMin = outMax = glm::vec3(0.0f);
            return;
        }

        outMin = glm::vec3(std::numeric_limits<float>::max());
        outMax = glm::vec3(std::numeric_limits<float>::lowest());

        for (const auto& v : vertices)
        {
            outMin = glm::min(outMin, v.position);
            outMax = glm::max(outMax, v.position);
        }
    }
}
