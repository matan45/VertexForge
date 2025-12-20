#pragma once
#include "Frustum.hpp"
#include <vector>
#include <algorithm>
#include <limits>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace math
{
    // BVH Node - stored in flat array for cache efficiency
    struct BVHNode
    {
        AABB bounds;

        // If leafCount > 0, this is a leaf node
        // firstChild is index into primitives array
        // If leafCount == 0, this is internal node
        // firstChild is index of left child (right child is firstChild + 1)
        uint32_t firstChild = 0;
        uint32_t leafCount = 0;
        uint32_t parent = UINT32_MAX;  // Parent node index (UINT32_MAX for root)

        bool isLeaf() const { return leafCount > 0; }
    };

    // Primitive reference - links to actual scene object
    struct BVHPrimitive
    {
        AABB bounds;           // World-space bounds
        uint32_t entityId;     // Entity identifier

        glm::vec3 getCenter() const { return bounds.getCenter(); }
    };

    // Bounding Volume Hierarchy for spatial queries
    class BVH
    {
    public:
        BVH() = default;

        // Build BVH from a list of primitives
        // Primitives should have world-space bounds
        void build(std::vector<BVHPrimitive>&& primitives)
        {
            primitives_ = std::move(primitives);
            nodes_.clear();
            entityToPrimitive_.clear();
            primitiveToLeaf_.clear();

            if (primitives_.empty())
            {
                return;
            }

            // Reserve estimated node count (2n-1 for n primitives)
            nodes_.reserve(primitives_.size() * 2);
            entityToPrimitive_.reserve(primitives_.size());
            primitiveToLeaf_.reserve(primitives_.size());

            // Create root node
            nodes_.push_back(BVHNode{});

            // Build recursively
            buildRecursive(0, 0, static_cast<uint32_t>(primitives_.size()));
        }

        // Query all primitives that intersect the frustum
        void queryFrustum(const Frustum& frustum, std::vector<uint32_t>& results) const
        {
            results.clear();
            queryFrustumAppend(frustum, results);
        }

        // Query and append results (doesn't clear - use for merging multiple BVH queries)
        void queryFrustumAppend(const Frustum& frustum, std::vector<uint32_t>& results) const
        {
            if (nodes_.empty() || !frustum.isInitialized())
            {
                // Return all primitives if no BVH or frustum
                for (const auto& prim : primitives_)
                {
                    results.push_back(prim.entityId);
                }
                return;
            }

            queryFrustumRecursive(0, frustum, results);
        }

        // Get statistics
        size_t getNodeCount() const { return nodes_.size(); }
        size_t getPrimitiveCount() const { return primitives_.size(); }

        // Check if BVH is built
        bool isBuilt() const { return !nodes_.empty(); }

        // Clear the BVH
        void clear()
        {
            nodes_.clear();
            primitives_.clear();
            entityToPrimitive_.clear();
            primitiveToLeaf_.clear();
        }

        // Update a single entity's bounds and refit affected nodes
        // Returns true if the entity was found and updated
        bool updateEntityBounds(uint32_t entityId, const AABB& newBounds)
        {
            auto it = entityToPrimitive_.find(entityId);
            if (it == entityToPrimitive_.end())
            {
                return false;
            }

            uint32_t primIdx = it->second;
            primitives_[primIdx].bounds = newBounds;

            // Find the leaf containing this primitive and refit upward
            auto leafIt = primitiveToLeaf_.find(primIdx);
            if (leafIt != primitiveToLeaf_.end())
            {
                refitFromNode(leafIt->second);
            }

            return true;
        }

        // Batch update multiple entities and refit once
        void updateEntitiesBounds(const std::unordered_map<uint32_t, AABB>& entityBounds)
        {
            std::unordered_set<uint32_t> affectedLeaves;

            for (const auto& [entityId, newBounds] : entityBounds)
            {
                auto it = entityToPrimitive_.find(entityId);
                if (it == entityToPrimitive_.end())
                {
                    continue;
                }

                uint32_t primIdx = it->second;
                primitives_[primIdx].bounds = newBounds;

                auto leafIt = primitiveToLeaf_.find(primIdx);
                if (leafIt != primitiveToLeaf_.end())
                {
                    affectedLeaves.insert(leafIt->second);
                }
            }

            // Refit from all affected leaves (will naturally merge at common ancestors)
            for (uint32_t leafIdx : affectedLeaves)
            {
                refitFromNode(leafIdx);
            }
        }

        // Check if an entity exists in the BVH
        bool hasEntity(uint32_t entityId) const
        {
            return entityToPrimitive_.find(entityId) != entityToPrimitive_.end();
        }

    private:
        std::vector<BVHNode> nodes_;
        std::vector<BVHPrimitive> primitives_;
        std::unordered_map<uint32_t, uint32_t> entityToPrimitive_;  // entityId -> primitive index
        std::unordered_map<uint32_t, uint32_t> primitiveToLeaf_;    // primitive index -> leaf node index

        // Refit bounds from a node up to the root
        void refitFromNode(uint32_t nodeIdx)
        {
            while (nodeIdx != UINT32_MAX)
            {
                BVHNode& node = nodes_[nodeIdx];

                if (node.isLeaf())
                {
                    // Recompute leaf bounds from primitives
                    node.bounds = computeBounds(node.firstChild, node.firstChild + node.leafCount);
                }
                else
                {
                    // Recompute internal node bounds from children
                    const BVHNode& left = nodes_[node.firstChild];
                    const BVHNode& right = nodes_[node.firstChild + 1];
                    node.bounds.min = glm::min(left.bounds.min, right.bounds.min);
                    node.bounds.max = glm::max(left.bounds.max, right.bounds.max);
                }

                nodeIdx = node.parent;
            }
        }

        // Build BVH node recursively using median split
        void buildRecursive(uint32_t nodeIdx, uint32_t start, uint32_t end, uint32_t parentIdx = UINT32_MAX)
        {
            BVHNode& node = nodes_[nodeIdx];
            node.parent = parentIdx;

            // Compute bounds for this node
            node.bounds = computeBounds(start, end);

            uint32_t primCount = end - start;

            // Leaf node threshold
            constexpr uint32_t maxLeafSize = 4;

            if (primCount <= maxLeafSize)
            {
                // Create leaf node
                node.firstChild = start;
                node.leafCount = primCount;

                // Build primitive-to-leaf and entity-to-primitive mappings
                for (uint32_t i = start; i < end; ++i)
                {
                    primitiveToLeaf_[i] = nodeIdx;
                    entityToPrimitive_[primitives_[i].entityId] = i;
                }
                return;
            }

            // Find best axis to split (longest extent)
            glm::vec3 extent = node.bounds.max - node.bounds.min;
            int axis = 0;
            if (extent.y > extent.x) axis = 1;
            if (extent.z > extent[axis]) axis = 2;

            // Sort primitives along chosen axis
            uint32_t mid = start + primCount / 2;

            std::nth_element(
                primitives_.begin() + start,
                primitives_.begin() + mid,
                primitives_.begin() + end,
                [axis](const BVHPrimitive& a, const BVHPrimitive& b)
                {
                    return a.getCenter()[axis] < b.getCenter()[axis];
                }
            );

            // Create child nodes
            uint32_t leftChild = static_cast<uint32_t>(nodes_.size());
            nodes_.push_back(BVHNode{});
            nodes_.push_back(BVHNode{});

            node.firstChild = leftChild;
            node.leafCount = 0;  // Internal node

            // Recurse with parent index
            buildRecursive(leftChild, start, mid, nodeIdx);
            buildRecursive(leftChild + 1, mid, end, nodeIdx);
        }

        // Compute combined AABB for a range of primitives
        AABB computeBounds(uint32_t start, uint32_t end) const
        {
            AABB bounds;
            bounds.min = glm::vec3(std::numeric_limits<float>::max());
            bounds.max = glm::vec3(std::numeric_limits<float>::lowest());

            for (uint32_t i = start; i < end; ++i)
            {
                bounds.min = glm::min(bounds.min, primitives_[i].bounds.min);
                bounds.max = glm::max(bounds.max, primitives_[i].bounds.max);
            }

            return bounds;
        }

        // Recursive frustum query
        void queryFrustumRecursive(uint32_t nodeIdx, const Frustum& frustum,
                                   std::vector<uint32_t>& results) const
        {
            const BVHNode& node = nodes_[nodeIdx];

            // Test node bounds against frustum
            if (!frustum.intersectsAABB(node.bounds))
            {
                return;  // Entire subtree is outside frustum
            }

            if (node.isLeaf())
            {
                // Add all primitives in this leaf
                // (could do per-primitive test here for tighter culling)
                for (uint32_t i = 0; i < node.leafCount; ++i)
                {
                    const auto& prim = primitives_[node.firstChild + i];
                    // Optional: per-primitive frustum test
                    if (frustum.intersectsAABB(prim.bounds))
                    {
                        results.push_back(prim.entityId);
                    }
                }
            }
            else
            {
                // Recurse into children
                queryFrustumRecursive(node.firstChild, frustum, results);
                queryFrustumRecursive(node.firstChild + 1, frustum, results);
            }
        }
    };
}
