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

        // Compute bounding box
        glm::vec3 bboxMin, bboxMax;
        computeBoundingBox(lod0.vertices, bboxMin, bboxMax);

        // Slightly shrink bbox to keep seeds inside the mesh volume
        glm::vec3 bboxPadding = (bboxMax - bboxMin) * 0.01f;
        glm::vec3 seedMin = bboxMin + bboxPadding;
        glm::vec3 seedMax = bboxMax - bboxPadding;

        // Generate Voronoi seed points
        auto seeds = generateSeeds(seedMin, seedMax, config);

        if (progressCallback)
        {
            progressCallback(0.05f, "Seeds generated");
        }

        // Build one fragment per Voronoi cell
        float progressPerCell = 0.85f / static_cast<float>(seeds.size());

        for (uint32_t i = 0; i < static_cast<uint32_t>(seeds.size()); ++i)
        {
            if (cancelFlag && cancelFlag->load(std::memory_order_relaxed))
            {
                result.errorMessage = "Cancelled";
                return result;
            }

            auto fragment = buildFragment(lod0.vertices, lod0.indices, seeds, i, config);

            // Only keep fragments that have geometry
            if (!fragment.mesh.lodLevels.empty() &&
                !fragment.mesh.lodLevels[0].vertices.empty() &&
                fragment.mesh.lodLevels[0].indices.size() >= 3)
            {
                result.fragments.push_back(std::move(fragment));
            }

            if (progressCallback)
            {
                progressCallback(0.05f + progressPerCell * (i + 1), "Fracturing");
            }
        }

        if (result.fragments.empty())
        {
            result.errorMessage = "No fragments generated";
            return result;
        }

        // Build connectivity graph
        buildConnectivityGraph(result.fragments, seeds);

        if (progressCallback)
        {
            progressCallback(0.95f, "Connectivity built");
        }

        // Name fragments
        for (size_t i = 0; i < result.fragments.size(); ++i)
        {
            result.fragments[i].mesh.name = "fragment_" + std::to_string(i);
        }

        result.success = true;

        if (progressCallback)
        {
            progressCallback(1.0f, "Complete");
        }

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
            meshesData.meshes.push_back(fragment.mesh);
        }

        return meshesData;
    }

    std::vector<glm::vec3> VoronoiFracture::generateSeeds(
        const glm::vec3& bboxMin,
        const glm::vec3& bboxMax,
        const FractureConfig& config)
    {
        std::vector<glm::vec3> seeds;
        std::mt19937 rng(config.randomSeed);

        switch (config.seedDistribution)
        {
        case SeedDistribution::Uniform:
        {
            // Jittered grid distribution
            uint32_t count = config.cellCount;
            glm::vec3 size = bboxMax - bboxMin;
            float volume = size.x * size.y * size.z;
            float cellVolume = volume / static_cast<float>(count);
            float cellSize = std::cbrt(cellVolume);

            uint32_t nx = std::max(1u, static_cast<uint32_t>(std::ceil(size.x / cellSize)));
            uint32_t ny = std::max(1u, static_cast<uint32_t>(std::ceil(size.y / cellSize)));
            uint32_t nz = std::max(1u, static_cast<uint32_t>(std::ceil(size.z / cellSize)));

            float dx = size.x / static_cast<float>(nx);
            float dy = size.y / static_cast<float>(ny);
            float dz = size.z / static_cast<float>(nz);

            std::uniform_real_distribution<float> jitterX(-dx * 0.4f, dx * 0.4f);
            std::uniform_real_distribution<float> jitterY(-dy * 0.4f, dy * 0.4f);
            std::uniform_real_distribution<float> jitterZ(-dz * 0.4f, dz * 0.4f);

            for (uint32_t z = 0; z < nz; ++z)
            {
                for (uint32_t y = 0; y < ny; ++y)
                {
                    for (uint32_t x = 0; x < nx; ++x)
                    {
                        glm::vec3 center = bboxMin + glm::vec3(
                            (x + 0.5f) * dx,
                            (y + 0.5f) * dy,
                            (z + 0.5f) * dz);

                        center.x += jitterX(rng);
                        center.y += jitterY(rng);
                        center.z += jitterZ(rng);

                        center = glm::clamp(center, bboxMin, bboxMax);
                        seeds.push_back(center);

                        if (seeds.size() >= config.cellCount)
                            goto done_uniform;
                    }
                }
            }
            done_uniform:

            // If grid produced fewer seeds than requested, add random ones
            std::uniform_real_distribution<float> distX(bboxMin.x, bboxMax.x);
            std::uniform_real_distribution<float> distY(bboxMin.y, bboxMax.y);
            std::uniform_real_distribution<float> distZ(bboxMin.z, bboxMax.z);
            while (seeds.size() < config.cellCount)
            {
                seeds.emplace_back(distX(rng), distY(rng), distZ(rng));
            }
            break;
        }

        case SeedDistribution::Clustered:
        {
            glm::vec3 size = bboxMax - bboxMin;
            float maxRadius = glm::length(size) * config.clusterParams.clusterRadius;

            std::uniform_real_distribution<float> distX(bboxMin.x, bboxMax.x);
            std::uniform_real_distribution<float> distY(bboxMin.y, bboxMax.y);
            std::uniform_real_distribution<float> distZ(bboxMin.z, bboxMax.z);
            std::normal_distribution<float> gaussian(0.0f, maxRadius * 0.33f);

            // Generate cluster centers
            uint32_t clusterCount = std::min(config.clusterParams.clusterCount, config.cellCount);
            std::vector<glm::vec3> clusterCenters;
            for (uint32_t c = 0; c < clusterCount; ++c)
            {
                clusterCenters.emplace_back(distX(rng), distY(rng), distZ(rng));
            }

            // Distribute seeds among clusters
            uint32_t seedsPerCluster = config.cellCount / clusterCount;
            uint32_t remainder = config.cellCount % clusterCount;

            for (uint32_t c = 0; c < clusterCount; ++c)
            {
                uint32_t count = seedsPerCluster + (c < remainder ? 1 : 0);
                for (uint32_t s = 0; s < count; ++s)
                {
                    glm::vec3 offset(gaussian(rng), gaussian(rng), gaussian(rng));
                    glm::vec3 seed = clusterCenters[c] + offset;
                    seed = glm::clamp(seed, bboxMin, bboxMax);
                    seeds.push_back(seed);
                }
            }
            break;
        }

        case SeedDistribution::ArtistPlaced:
        {
            seeds = config.artistSeeds;
            break;
        }
        }

        return seeds;
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

        const glm::vec3& cellSeed = seeds[cellIndex];

        // Start with the full mesh
        std::vector<resource::Vertex> currentVertices = vertices;
        std::vector<uint32_t> currentIndices = indices;

        // Collect all cut edges across all clipping planes for capping
        struct PlaneClipInfo
        {
            glm::vec4 plane;
            std::vector<std::pair<uint32_t, uint32_t>> cutEdges;
            uint32_t neighborSeedIndex;
        };
        std::vector<PlaneClipInfo> clipInfos;

        // Sort other seeds by distance to this cell's seed (nearest first for early exit)
        std::vector<uint32_t> sortedSeeds;
        sortedSeeds.reserve(seeds.size() - 1);
        for (uint32_t j = 0; j < static_cast<uint32_t>(seeds.size()); ++j)
        {
            if (j != cellIndex) sortedSeeds.push_back(j);
        }
        std::sort(sortedSeeds.begin(), sortedSeeds.end(),
            [&](uint32_t a, uint32_t b)
            {
                return glm::distance(cellSeed, seeds[a]) < glm::distance(cellSeed, seeds[b]);
            });

        // Clip against each bisecting plane
        for (uint32_t j : sortedSeeds)
        {
            if (currentVertices.empty() || currentIndices.size() < 3)
                break;

            // Bisecting plane: midpoint between seeds, normal pointing from j toward cellIndex
            glm::vec3 midpoint = (cellSeed + seeds[j]) * 0.5f;
            glm::vec3 normal = glm::normalize(cellSeed - seeds[j]);
            float d = -glm::dot(normal, midpoint);
            glm::vec4 plane(normal, d);

            auto clipResult = MeshClipper::clipToHalfSpace(currentVertices, currentIndices, plane);

            if (!clipResult.didClip)
            {
                // Mesh entirely on positive side, no clipping needed for this plane
                continue;
            }

            if (clipResult.vertices.empty() || clipResult.indices.size() < 3)
            {
                // Mesh entirely clipped away
                currentVertices.clear();
                currentIndices.clear();
                break;
            }

            // Store clip info for capping
            if (!clipResult.cutEdges.empty())
            {
                clipInfos.push_back({plane, std::move(clipResult.cutEdges), j});
            }

            currentVertices = std::move(clipResult.vertices);
            currentIndices = std::move(clipResult.indices);
        }

        if (currentVertices.empty() || currentIndices.size() < 3)
        {
            return fragment;
        }

        // Cap all cut faces (inner fracture surfaces)
        for (const auto& clipInfo : clipInfos)
        {
            capCutFace(currentVertices, currentIndices,
                       clipInfo.cutEdges, clipInfo.plane, config.innerUVScale);
        }

        // Build the fragment mesh data
        resource::LODLevel lod0;
        lod0.vertices = std::move(currentVertices);
        lod0.indices = std::move(currentIndices);

        fragment.mesh.lodLevels.push_back(std::move(lod0));

        // Compute center of mass and volume
        const auto& finalVerts = fragment.mesh.lodLevels[0].vertices;
        const auto& finalIndices = fragment.mesh.lodLevels[0].indices;
        fragment.centerOfMass = computeCenterOfMass(finalVerts, finalIndices);
        fragment.volume = computeVolume(finalVerts, finalIndices);

        return fragment;
    }

    void VoronoiFracture::buildConnectivityGraph(
        std::vector<FragmentData>& fragments,
        const std::vector<glm::vec3>& seeds)
    {
        // Build a mapping from cellIndex to fragment index
        std::unordered_map<uint32_t, uint32_t> cellToFragment;
        for (uint32_t i = 0; i < static_cast<uint32_t>(fragments.size()); ++i)
        {
            cellToFragment[fragments[i].cellIndex] = i;
        }

        // Two cells are neighbors if their Voronoi cells share a face
        // This means they are "Delaunay neighbors" — we approximate by checking
        // if the bisecting plane between their seeds actually clipped geometry
        // Since we don't store that info directly, use proximity:
        // Two fragments are neighbors if no other seed is closer to both seeds' midpoint
        for (uint32_t i = 0; i < static_cast<uint32_t>(fragments.size()); ++i)
        {
            uint32_t cellI = fragments[i].cellIndex;

            for (uint32_t j = i + 1; j < static_cast<uint32_t>(fragments.size()); ++j)
            {
                uint32_t cellJ = fragments[j].cellIndex;

                glm::vec3 midpoint = (seeds[cellI] + seeds[cellJ]) * 0.5f;
                float distIJ = glm::distance(seeds[cellI], seeds[cellJ]);

                // Check if any other seed is closer to the midpoint than both seeds
                bool isNeighbor = true;
                for (uint32_t k = 0; k < static_cast<uint32_t>(seeds.size()); ++k)
                {
                    if (k == cellI || k == cellJ) continue;
                    float distK = glm::distance(seeds[k], midpoint);
                    if (distK < distIJ * 0.5f - 1e-5f)
                    {
                        isNeighbor = false;
                        break;
                    }
                }

                if (isNeighbor)
                {
                    // Estimate shared area from fragment bounding boxes
                    // (approximate — exact area would require tracking cap polygons)
                    glm::vec3 minI, maxI, minJ, maxJ;
                    computeBoundingBox(fragments[i].mesh.lodLevels[0].vertices, minI, maxI);
                    computeBoundingBox(fragments[j].mesh.lodLevels[0].vertices, minJ, maxJ);

                    glm::vec3 sizeI = maxI - minI;
                    glm::vec3 sizeJ = maxJ - minJ;
                    float avgSize = (glm::length(sizeI) + glm::length(sizeJ)) * 0.5f;
                    float sharedArea = avgSize * avgSize * 0.25f;

                    fragments[i].neighbors.push_back({j, sharedArea});
                    fragments[j].neighbors.push_back({i, sharedArea});
                }
            }
        }
    }

    glm::vec3 VoronoiFracture::computeCenterOfMass(
        const std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices)
    {
        if (indices.size() < 3)
        {
            // Fallback: average of all vertex positions
            glm::vec3 sum(0.0f);
            for (const auto& v : vertices)
            {
                sum += v.position;
            }
            return vertices.empty() ? glm::vec3(0.0f) : sum / static_cast<float>(vertices.size());
        }

        // Volume-weighted centroid using signed tetrahedra volumes (origin as reference)
        glm::vec3 weightedSum(0.0f);
        float totalVolume = 0.0f;

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const glm::vec3& a = vertices[indices[i]].position;
            const glm::vec3& b = vertices[indices[i + 1]].position;
            const glm::vec3& c = vertices[indices[i + 2]].position;

            float tetVolume = glm::dot(a, glm::cross(b, c)) / 6.0f;
            glm::vec3 tetCentroid = (a + b + c) * 0.25f;

            weightedSum += tetCentroid * tetVolume;
            totalVolume += tetVolume;
        }

        if (std::abs(totalVolume) < 1e-10f)
        {
            // Degenerate mesh, fallback to vertex average
            glm::vec3 sum(0.0f);
            for (const auto& v : vertices)
            {
                sum += v.position;
            }
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
            const glm::vec3& a = vertices[indices[i]].position;
            const glm::vec3& b = vertices[indices[i + 1]].position;
            const glm::vec3& c = vertices[indices[i + 2]].position;

            volume += glm::dot(a, glm::cross(b, c)) / 6.0f;
        }
        return std::abs(volume);
    }

    void VoronoiFracture::capCutFace(
        std::vector<resource::Vertex>& vertices,
        std::vector<uint32_t>& indices,
        const std::vector<std::pair<uint32_t, uint32_t>>& cutEdges,
        const glm::vec4& plane,
        float uvScale)
    {
        if (cutEdges.empty())
            return;

        // Build ordered polygon loops from cut edges
        // Each cut edge is an undirected edge; we need to chain them into loops

        // Build adjacency: vertex -> connected vertices via cut edges
        std::unordered_map<uint32_t, std::vector<uint32_t>> adjacency;
        for (const auto& [a, b] : cutEdges)
        {
            adjacency[a].push_back(b);
            adjacency[b].push_back(a);
        }

        std::unordered_set<uint32_t> visited;
        std::vector<std::vector<uint32_t>> loops;

        // Extract loops by walking the adjacency graph
        for (const auto& [startVertex, _] : adjacency)
        {
            if (visited.count(startVertex)) continue;

            std::vector<uint32_t> loop;
            uint32_t current = startVertex;
            uint32_t prev = UINT32_MAX;

            while (true)
            {
                if (visited.count(current) && !loop.empty())
                    break;

                visited.insert(current);
                loop.push_back(current);

                const auto& neighbors = adjacency[current];
                uint32_t next = UINT32_MAX;
                for (uint32_t n : neighbors)
                {
                    if (n != prev)
                    {
                        next = n;
                        break;
                    }
                }

                if (next == UINT32_MAX)
                    break;

                prev = current;
                current = next;
            }

            if (loop.size() >= 3)
            {
                loops.push_back(std::move(loop));
            }
        }

        // Build local 2D coordinate system on the plane for UV generation
        glm::vec3 planeNormal = glm::vec3(plane);
        glm::vec3 tangent, bitangent;

        // Choose tangent perpendicular to normal
        if (std::abs(planeNormal.y) < 0.99f)
        {
            tangent = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), planeNormal));
        }
        else
        {
            tangent = glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), planeNormal));
        }
        bitangent = glm::cross(planeNormal, tangent);

        // Flip normal inward for the cap face (the cap faces into the fragment)
        glm::vec3 capNormal = -planeNormal;

        // Triangulate each loop using fan triangulation (Voronoi cells are convex)
        for (const auto& loop : loops)
        {
            // Create new vertices for the cap with flat normal and generated UVs
            uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

            for (uint32_t idx : loop)
            {
                resource::Vertex capVertex;
                capVertex.position = vertices[idx].position;
                capVertex.normal = capNormal;

                // Planar projection UV
                capVertex.texCoords.x = glm::dot(capVertex.position, tangent) * uvScale;
                capVertex.texCoords.y = glm::dot(capVertex.position, bitangent) * uvScale;

                capVertex.boneIndices = glm::ivec4(-1);
                capVertex.boneWeights = glm::vec4(0.0f);

                vertices.push_back(capVertex);
            }

            // Fan triangulation from first vertex
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
