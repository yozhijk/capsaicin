#include "camera.h"
#include "lighting.h"
#include "math_functions.h"
#include "sampling.h"
#include "scene.h"
#include "shading.h"
#include "aabb.h"

#define TILE_SIZE 8

struct Constants
{
    uint width;
    uint height;
    uint frame_count;
    uint padding;

    float4 scene_aabb_min;
    float4 scene_aabb_max;
};

struct OctreeNode
{
    uint value;
    uint count;
    uint leaf;
    uint padding;
    uint children[8];
};

ConstantBuffer<Constants> g_constants : register(b0);
ConstantBuffer<Camera> g_camera : register(b1);
RWStructuredBuffer<uint> g_grid: register(u0);
RWStructuredBuffer<OctreeNode> g_octree: register(u1);
RWTexture2D<float4> g_output: register(u2);

struct Ray
{
    float3 o;
    float tmin;
    float3 d;
    float tmax;
};

Ray CreatePrimaryRay(in uint2 xy, in uint2 dim)
{
    float2 s = 0.5f;

    // Calculate [0..1] image plane sample
    float2 img_sample = (float2(xy) + s) / float2(dim);

    // Transform into [-0.5, 0.5]
    float2 h_sample = img_sample - float2(0.5f, 0.5f);
    // Transform into [-dim/2, dim/2]
    float2 c_sample = h_sample * g_camera.sensor_size;

    Ray my_ray;
    // Calculate direction to image plane
    my_ray.d =
        normalize(g_camera.focal_length * g_camera.forward + c_sample.x * g_camera.right + c_sample.y * g_camera.up);
    // Origin == camera position + nearz * d
    my_ray.o = g_camera.position;
    my_ray.tmin = 0.0f;
    my_ray.tmax = 1e6f;
    return my_ray;
}

struct StackEntry
{
    uint parent;
    float t_max;
};

#define STACK_SIZE 23
#define MAX_ITER 10000

float copysign(float v, float s)
{
    return s > 0.f ? v : -v;
}

float4 TraceOctree(in Ray r)
{
    Ray ray = r;

    // Setup
    AABB aabb;
    aabb.pmin = g_constants.scene_aabb_min.xyz;
    aabb.pmax = g_constants.scene_aabb_max.xyz;

    float2 t = IntersectAABB(ray.o, rcp(ray.d), aabb.pmin, aabb.pmax, ray.tmax);

    if (t.x >= t.y)
    {
        return 0.f;
    }

    // Rescale ray.
    float3 old_o = ray.o;
    float3 old_d = ray.d;
    ray.o = old_o + old_d * t.x;
    ray.d = old_d * (t.y - t.x);
    ray.tmin = 0.f;
    ray.tmax = 1.f;

    // Scale ray based on AABB : scene moves to [1..2]
    float3 scene_scale = CalculateAABBExtent(aabb);
    ray.o -= aabb.pmin;
    ray.d *= rcp(scene_scale);
    ray.o *= rcp(scene_scale);
    ray.o += 1.f;

    // Stack
    StackEntry stack[STACK_SIZE];

    uint iter = 0;
    uint hit_addr = ~0u;

    const float eps = 1e-5f;
    if (abs(ray.d.x) < eps) ray.d.x = copysign(eps, ray.d.x);
    if (abs(ray.d.y) < eps) ray.d.y = copysign(eps, ray.d.y);
    if (abs(ray.d.z) < eps) ray.d.z = copysign(eps, ray.d.z);

    // Precompute the coefficients of tx(x), ty(y), and tz(z).
    // The octree is assumed to reside at coordinates [1, 2].
    float tx_coef = 1.0f / -abs(ray.d.x);
    float ty_coef = 1.0f / -abs(ray.d.y);
    float tz_coef = 1.0f / -abs(ray.d.z);
    float tx_bias = tx_coef * ray.o.x;
    float ty_bias = ty_coef * ray.o.y;
    float tz_bias = tz_coef * ray.o.z;

    // Select octant mask to mirror the coordinate system so
    // that ray direction is negative along each axis.
    int octant_mask = 0;
    if (ray.d.x > 0.0f) {octant_mask ^= 1; tx_bias = 3.0f * tx_coef - tx_bias;}
    if (ray.d.y > 0.0f) {octant_mask ^= 2; ty_bias = 3.0f * ty_coef - ty_bias;}
    if (ray.d.z > 0.0f) {octant_mask ^= 4; tz_bias = 3.0f * tz_coef - tz_bias;}

    // Initialize the active span of t-values.
    float t_min = max(max(2.0f * tx_coef - tx_bias, 2.0f * ty_coef - ty_bias), 2.0f * tz_coef - tz_bias);
    float t_max = min(min(tx_coef - tx_bias, ty_coef - ty_bias), tz_coef - tz_bias);
    float h = t_max;
    t_min = max(t_min, 0.0f);
    t_max = min(t_max, 1.0f);

    // Initialize the current voxel to the first child of the root.
    int    parent           = 0;
    int    idx              = 0;
    float3 pos              = float3(1.0f, 1.0f, 1.0f);
    int    scale            = STACK_SIZE - 1;
    float  scale_exp2       = 0.5f;

    if (1.5f * tx_coef - tx_bias > t_min) {idx ^= 1; pos.x = 1.5f;}
    if (1.5f * ty_coef - ty_bias > t_min) {idx ^= 2; pos.y = 1.5f;}
    if (1.5f * tz_coef - tz_bias > t_min) {idx ^= 4; pos.z = 1.5f;}

    // Traverse voxels along the ray as long as the current voxel
    // stays within the octree.
    while (scale < STACK_SIZE)
    {
        iter++;
        if (iter > MAX_ITER)
            break;

        // Determine maximum t-value of the cube by evaluating
        // tx(), ty(), and tz() at its corner.
        float tx_corner = pos.x * tx_coef - tx_bias;
        float ty_corner = pos.y * ty_coef - ty_bias;
        float tz_corner = pos.z * tz_coef - tz_bias;
        float tc_max = min(min(tx_corner, ty_corner), tz_corner);

        // Process voxel if the corresponding bit in valid mask is set
        // and the active t-span is non-empty.
        int child_index = idx ^ octant_mask; // permute child slots based on the mirroring
        if (g_octree[parent].children[child_index] != ~0u && t_min <= t_max)
        {
            // Terminate if the voxel is small enough.
            uint addr = g_octree[parent].children[child_index];
            if (g_octree[addr].leaf)
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

                if (tc_max < h)
                {
                    StackEntry entry;
                    entry.parent = parent;
                    entry.t_max = t_max;
                    stack[scale] = entry;
                }

                h = tc_max;

                parent = addr;

                idx = 0;
                scale--;
                scale_exp2 = half;

                if (tx_center > t_min) {idx ^= 1; pos.x += scale_exp2;}
                if (ty_center > t_min) {idx ^= 2; pos.y += scale_exp2;}
                if (tz_center > t_min) {idx ^= 4; pos.z += scale_exp2;}
                t_max = tv_max;
                continue;
            }
        }

        // ADVANCE
        int step_mask = 0;
        if (tx_corner <= tc_max) step_mask ^= 1, pos.x -= scale_exp2;
        if (ty_corner <= tc_max) step_mask ^= 2, pos.y -= scale_exp2;
        if (tz_corner <= tc_max) step_mask ^= 4, pos.z -= scale_exp2;

        // Update active t-span and flip bits of the child slot index.
        t_min = tc_max;
        idx ^= step_mask;

        // Proceed with pop if the bit flips disagree with the ray direction.
        if ((idx & step_mask) != 0)
        {
            // POP
            // Find the highest differing bit between the two positions.
            uint differing_bits = 0;
            if ((step_mask & 1) != 0) differing_bits |= asint(pos.x) ^ asint(pos.x + scale_exp2);
            if ((step_mask & 2) != 0) differing_bits |= asint(pos.y) ^ asint(pos.y + scale_exp2);
            if ((step_mask & 4) != 0) differing_bits |= asint(pos.z) ^ asint(pos.z + scale_exp2);
            scale = (asint((float)differing_bits) >> 23) - 127; // position of the highest bit
            scale_exp2 = asfloat((scale - STACK_SIZE + 127) << 23); // exp2f(scale - s_max)

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
            idx  = (shx & 1) | ((shy & 1) << 1) | ((shz & 1) << 2);

            // Prevent same parent from being stored again and invalidate cached child descriptor.
            h = 0.0f;
        }
    }

    if (hit_addr == ~0u)
        return 0.f;
    else
    {
        return float4(g_octree[hit_addr].value / 30.f, 0.f, 0.f, 1.f);
    }
}

float4 TraceGrid(in Ray ray)
{
    // Setup
    AABB grid_aabb;
    grid_aabb.pmin = g_constants.scene_aabb_min.xyz;
    grid_aabb.pmax = g_constants.scene_aabb_max.xyz;

    int3 step = int3(sign(ray.d));
    float3 grid_extent = CalculateAABBExtent(grid_aabb);
    float3 voxel_size = grid_extent / 32.f;
    float3 dt = voxel_size * abs(rcp(ray.d));

    float2 t = IntersectAABB(ray.o, rcp(ray.d), grid_aabb.pmin, grid_aabb.pmax, ray.tmax);

    if (t.x >= t.y)
    {
        return 0.f;
    }

    // Intersection with aabb
    float3 start_point = ray.o + ray.d * t.x;
    float3 end_point = ray.o + ray.d * t.y;

    float3 current_voxel = floor((start_point - grid_aabb.pmin) / voxel_size);
    float3 next_plane = ((current_voxel + 0.5f * (step + 1.f)) * voxel_size) + grid_aabb.pmin;
    float3 t_next = (next_plane - ray.o) * rcp(ray.d);

    uint result = 0;
    uint steps = 0;

    while (1)
    {
        if (any(current_voxel < 0.f) || any (current_voxel > 31.f)) break;

        ++steps;
        uint voxel_idx = current_voxel.z * 32 * 32 + current_voxel.y * 32 + current_voxel.x;

        if (g_grid[voxel_idx] > 0)
        {
            result += g_grid[voxel_idx];
            break;
        }

        if (t_next.x < t_next.y)
        {
            if (t_next.x < t_next.z)
            {
                current_voxel.x += step.x;
                t_next.x += dt.x;
            }
            else
            {
                current_voxel.z += step.z;
                t_next.z += dt.z;
            }
        }
        else
        {
            if (t_next.y < t_next.z)
            {
                current_voxel.y += step.y;
                t_next.y += dt.y;
            }
            else
            {
                current_voxel.z +=  step.z;
                t_next.z += dt.z;
            }
        }
    }

    return float4(result / 30.f, 0.f, 0.f, steps);
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void Visualize(in uint2 gidx: SV_DispatchThreadID,
               in uint2 lidx: SV_GroupThreadID,
               in uint2 bidx: SV_GroupID)
{
    Ray ray = CreatePrimaryRay(gidx, uint2(g_constants.width, g_constants.height));

    float4 color = TraceOctree(ray);

    g_output[gidx] = color;
}