#pragma once

#include "src/common.h"
#include "src/utils/vector_math.h"

namespace capsaicin
{
template <typename T>
class VoxelGrid
{
public:
    VoxelGrid(int3 dim) : dim_(dim), data_(dim.x * dim.y * dim.z) {}

    int3 dim() const { return dim_; }

    T& voxel(const int3& idx) { return data_[idx.z * dim_.x * dim_.y + idx.y * dim_.x + idx.x]; }
    const T& voxel(const int3& idx) const { return data_[idx.z * dim_.x * dim_.y + idx.y * dim_.x + idx.x]; }

    template <typename F>
    void Merge(const VoxelGrid<T>& other, F&& f)
    {
        if (dim_ != other.dim_)
            throw std::runtime_error("VoxelGrid: size mismatch");

        for (auto i = 0; i < dim_.x; ++i)
            for (auto j = 0; j < dim_.y; ++j)
                for (auto k = 0; k < dim_.z; ++k)
                {
                    voxel(int3{i, j, k}) += other.voxel(int3{i, j, k});
                }
    }

    const T* data() const { return data_.data(); }
    size_t data_size() const { return data_.size() * sizeof(T); }

private:
    int3 dim_;
    std::vector<T> data_ = {T()};
};

//=====================================================================================================================
// The following two functions are from
// http://devblogs.nvidia.com/parallelforall/thinking-parallel-part-iii-tree-construction-gpu/
// Expands a 10-bit integer into 30 bits
// by inserting 2 zeros after each bit.
inline uint32_t ExpandBits(uint32_t v)
{
    v = (v * 0x00010001u) & 0xFF0000FFu;
    v = (v * 0x00000101u) & 0x0F00F00Fu;
    v = (v * 0x00000011u) & 0xC30C30C3u;
    v = (v * 0x00000005u) & 0x49249249u;
    return v;
}

//=====================================================================================================================
// Calculates a 30-bit Morton code for the
// given 3D point located within the unit cube [0,1].
inline uint32_t CalculateMortonCode(uint32_t x, uint32_t y, uint32_t z)
{
    auto xx = ExpandBits(x);
    auto yy = ExpandBits(y);
    auto zz = ExpandBits(z);
    return xx + yy * 2 + zz * 4;
}

template <typename T>
class VoxelOctree
{
public:
    VoxelOctree(const VoxelGrid<T>& grid) { Build(grid); }

    struct Node
    {
        T value = T();
        uint32_t count = 0;
        uint32_t leaf = 0;
        uint32_t code = 0;
        uint32_t children[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
    };

    size_t node_count() const { return nodes_.size(); }
    const Node* data() const { return nodes_.data(); }

    // private:
    struct VoxelRef
    {
        uint32_t code;
        uint32_t x, y, z;
    };

    void Build(const VoxelGrid<T>& grid)
    {
        auto dim = grid.dim();
        auto max_dim = std::max(std::max(dim.x, dim.y), dim.z);
        num_levels_ = std::log2(max_dim) + 1;

        std::vector<VoxelRef> voxel_refs;
        voxel_refs.reserve(dim.x * dim.y * dim.z);

        for (uint32_t i = 0; i < dim.x; ++i)
            for (uint32_t j = 0; j < dim.y; ++j)
                for (uint32_t k = 0; k < dim.z; ++k)
                {
                    if (grid.voxel(int3{(int)i, (int)j, (int)k}) > T())
                    {
                        voxel_refs.push_back(VoxelRef{CalculateMortonCode(i, j, k), i, j, k});
                    }
                }

        std::sort(voxel_refs.begin(), voxel_refs.end(), [](const VoxelRef& v1, const VoxelRef& v2) {
            return v1.code < v2.code;
        });

        root_index_ = BuildNode(voxel_refs.begin(), voxel_refs.end(), grid, 0);
    }

    template <typename I>
    uint32_t BuildNode(I begin, I end, const VoxelGrid<T>& grid, uint32_t level)
    {
        uint32_t index = nodes_.size();
        nodes_.push_back(Node{});

        if (level == num_levels_ - 1)
        {
            assert(std::distance(begin, end) == 1);
            nodes_[index].leaf = 1;
            nodes_[index].count = 1;
            nodes_[index].code = begin->code;
            nodes_[index].value = grid.voxel(int3{(int)begin->x, (int)begin->y, (int)begin->z});
            return index;
        }

        uint32_t bitshift = (num_levels_ - 2 - level) * 3;

        I interval_start = begin;
        uint32_t interval_len = 0;
        uint32_t current_child = (begin->code >> bitshift) & 7;

        for (auto i = begin; i != end; ++i)
        {
            uint32_t child = (i->code >> bitshift) & 7;

            if (child != current_child && interval_len > 0)
            {
                nodes_[index].children[current_child] = BuildNode(interval_start, i, grid, level + 1);
                interval_start = i;
                interval_len = 0;
                current_child = child;
            }

            ++interval_len;
            nodes_[index].value += grid.voxel(int3{(int)i->x, (int)i->y, (int)i->z});
        }

        if (interval_len > 0)
        {
            nodes_[index].children[current_child] = BuildNode(interval_start, end, grid, level + 1);
        }

        nodes_[index].value /= std::distance(begin, end);
        nodes_[index].count = std::distance(begin, end);

        return index;
    }

    void TraverseNode(uint32_t index, uint32_t code, uint32_t level)
    {
        if (level == num_levels_ - 1)
        {
            // Set value
            assert(nodes_[index].code == code);
        }
        else
        {
            uint32_t bitshift = ((num_levels_ - 2) - level) * 3;

            for (uint32_t i = 0; i < 8; ++i)
            {
                if (nodes_[index].children[i] != ~0u)
                {
                    TraverseNode(nodes_[index].children[i], code + (i << bitshift), level + 1);
                }
            }
        }
    }

    void Trace(const Aabb& aabb)
    {
        using namespace std;

        auto asint = [](float v) { return *((int*)&v); };
        auto asfloat = [](int v) { return *((float*)&v); };
        auto copysign = [](float v, float s) { return s >= 0.f ? v : -v; };

        struct StackEntry
        {
            uint32_t parent;
            float t_max;
        };

        constexpr uint32_t CAST_STACK_DEPTH = 23;
        constexpr uint32_t MAX_RAYCAST_ITERATIONS = 10000;

        float3 o = float3(0, 15, 0);
        float3 d = float3(1, 0, 0);
        float tmax = 1000.f;

        float2 t = IntersectAabb(aabb, o, d.inverse(), tmax);

        if (t.x >= t.y)
        {
            return;
        }

        // Rescale ray.
        float3 old_o = o;
        float3 old_d = d;
        o = old_o + old_d * t.x;
        d = old_d * (t.y - t.x);
        tmax = 1.f;

        // Scale ray based on AABB : scene moves to [1..2]
        float3 scene_scale = aabb.extents();
        o -= aabb.pmin;
        o *= scene_scale.inverse();
        d *= scene_scale.inverse();
        o += float3(1.f, 1.f, 1.f);

        // Stack
        StackEntry stack[CAST_STACK_DEPTH];

        uint32_t iter = 0;
        uint32_t hit_addr = ~0u;

        float eps = 1e-5f;
        if (abs(d.x) < eps)
            d.x = copysign(eps, d.x);
        if (abs(d.y) < eps)
            d.y = copysign(eps, d.y);
        if (abs(d.z) < eps)
            d.z = copysign(eps, d.z);

        // Precompute the coefficients of tx(x), ty(y), and tz(z).
        // The octree is assumed to reside at coordinates [1, 2].
        float tx_coef = 1.0f / -abs(d.x);
        float ty_coef = 1.0f / -abs(d.y);
        float tz_coef = 1.0f / -abs(d.z);
        float tx_bias = tx_coef * o.x;
        float ty_bias = ty_coef * o.y;
        float tz_bias = tz_coef * o.z;

        // Select octant mask to mirror the coordinate system so
        // that ray direction is negative along each axis.
        int octant_mask = 7;
        if (d.x > 0.0f)
        {
            octant_mask ^= 1;
            tx_bias = 3.0f * tx_coef - tx_bias;
        }
        if (d.y > 0.0f)
        {
            octant_mask ^= 2;
            ty_bias = 3.0f * ty_coef - ty_bias;
        }
        if (d.z > 0.0f)
        {
            octant_mask ^= 4;
            tz_bias = 3.0f * tz_coef - tz_bias;
        }

        // Initialize the active span of t-values.
        float t_min = max(max(2.0f * tx_coef - tx_bias, 2.0f * ty_coef - ty_bias), 2.0f * tz_coef - tz_bias);
        float t_max = min(min(tx_coef - tx_bias, ty_coef - ty_bias), tz_coef - tz_bias);
        float h = t_max;
        t_min = max(t_min, 0.0f);
        t_max = min(t_max, 1.0f);

        // Initialize the current voxel to the first child of the root.
        int parent = 0;
        int idx = 0;
        float3 pos = float3(1.0f, 1.0f, 1.0f);
        int scale = CAST_STACK_DEPTH - 1;
        float scale_exp2 = 0.5f;

        if (1.5f * tx_coef - tx_bias > t_min)
        {
            idx ^= 1;
            pos.x = 1.5f;
        }
        if (1.5f * ty_coef - ty_bias > t_min)
        {
            idx ^= 2;
            pos.y = 1.5f;
        }
        if (1.5f * tz_coef - tz_bias > t_min)
        {
            idx ^= 4;
            pos.z = 1.5f;
        }

        // Traverse voxels along the ray as long as the current voxel
        // stays within the octree.
        while (scale < CAST_STACK_DEPTH)
        {
            iter++;
            if (iter > MAX_RAYCAST_ITERATIONS)
                break;

            // Determine maximum t-value of the cube by evaluating
            // tx(), ty(), and tz() at its corner.
            float tx_corner = pos.x * tx_coef - tx_bias;
            float ty_corner = pos.y * ty_coef - ty_bias;
            float tz_corner = pos.z * tz_coef - tz_bias;
            float tc_max = min(min(tx_corner, ty_corner), tz_corner);

            // Process voxel if the corresponding bit in valid mask is set
            // and the active t-span is non-empty.
            int child_index = idx ^ octant_mask;  // permute child slots based on the mirroring
            if (nodes_[parent].children[child_index] != ~0u && t_min <= t_max)
            {
                // Terminate if the voxel is small enough.
                uint32_t addr = nodes_[parent].children[child_index];
                if (nodes_[addr].leaf)
                {
                    hit_addr = addr;
                    break;
                }

                // INTERSECT
                // Intersect active t-span with the cube and evaluate
                // tx(), ty(), and tz() at the center of the voxel.
                float tv_max = min(t_max, tc_max);
                float half = scale_exp2 * 0.5f;
                float tx_center = half * tx_coef + tx_corner;
                float ty_center = half * ty_coef + ty_corner;
                float tz_center = half * tz_coef + tz_corner;

                // Descend to the first child if the resulting t-span is non-empty.
                if (t_min <= tv_max)
                {
                    // PUSH
                    // Write current parent to the stack.
                    //#ifndef DISABLE_PUSH_OPTIMIZATION
                    //                    if (tc_max < h)
                    //#endif
                    {
                        StackEntry entry;
                        entry.parent = parent;
                        entry.t_max = t_max;
                        stack[scale] = entry;
                    }

                    h = tc_max;

                    // Find child descriptor corresponding to the current voxel.
                    parent = addr;

                    // Select child voxel that the ray enters first.

                    idx = 0;
                    scale--;
                    scale_exp2 = half;

                    if (tx_center > t_min)
                    {
                        idx ^= 1;
                        pos.x += scale_exp2;
                    }
                    if (ty_center > t_min)
                    {
                        idx ^= 2;
                        pos.y += scale_exp2;
                    }
                    if (tz_center > t_min)
                    {
                        idx ^= 4;
                        pos.z += scale_exp2;
                    }

                    t_max = tv_max;
                    continue;
                }
            }

            // ADVANCE
            // Step along the ray.
            int step_mask = 0;
            if (tx_corner <= tc_max)
                step_mask ^= 1, pos.x -= scale_exp2;
            if (ty_corner <= tc_max)
                step_mask ^= 2, pos.y -= scale_exp2;
            if (tz_corner <= tc_max)
                step_mask ^= 4, pos.z -= scale_exp2;

            // Update active t-span and flip bits of the child slot index.
            t_min = tc_max;
            idx ^= step_mask;

            // Proceed with pop if the bit flips disagree with the ray direction.
            if ((idx & step_mask) != 0)
            {
                // POP
                // Find the highest differing bit between the two positions.
                uint32_t differing_bits = 0;
                if ((step_mask & 1) != 0)
                    differing_bits |= asint(pos.x) ^ asint(pos.x + scale_exp2);
                if ((step_mask & 2) != 0)
                    differing_bits |= asint(pos.y) ^ asint(pos.y + scale_exp2);
                if ((step_mask & 4) != 0)
                    differing_bits |= asint(pos.z) ^ asint(pos.z + scale_exp2);
                scale = (asint((float)differing_bits) >> 23) - 127;            // position of the highest bit
                scale_exp2 = asfloat((scale - CAST_STACK_DEPTH + 127) << 23);  // exp2f(scale - s_max)

                // Restore parent voxel from the stack.
                StackEntry entry = stack[scale];
                parent = entry.parent;
                t_max = entry.t_max;

                // Round cube position and extract child slot index.
                int shx = asint(pos.x) >> scale;
                int shy = asint(pos.y) >> scale;
                int shz = asint(pos.z) >> scale;
                pos.x = asfloat(shx << scale);
                pos.y = asfloat(shy << scale);
                pos.z = asfloat(shz << scale);
                idx = (shx & 1) | ((shy & 1) << 1) | ((shz & 1) << 2);

                // Prevent same parent from being stored again and invalidate cached child descriptor.
                h = 0.0f;
            }
        }
    }

    std::vector<Node> nodes_;
    uint32_t root_index_ = ~0u;
    uint32_t num_levels_ = 0;
};
}  // namespace capsaicin