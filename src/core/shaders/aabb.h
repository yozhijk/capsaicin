#ifndef AABB_H
#define AABB_H

#include "math_functions.h"

struct AABB
{
    float3 pmin;
    float3 pmax;
};

// AABB center.
float3 CalculateAABBCenter(in AABB aabb) { return 0.5f * (aabb.pmin + aabb.pmax); }

// AABB extent
float3 CalculateAABBExtent(in AABB aabb) { return aabb.pmax - aabb.pmin; }

// Clip a point to an AABB.
float3 ClipToAABB(in AABB aabb, in float3 p)
{
    float3 c = CalculateAABBCenter(aabb);
    float3 radius = 0.5f * CalculateAABBExtent(aabb);
    float3 dc = p - c;
    float3 clip = dc / (radius + 1e-5f);

    float max_extent = max_component(abs(clip));
    return (max_extent > 1.f) ? (c + dc / max_extent) : p;
}

float2 IntersectAABB(float3 ray_origin, float3 ray_inv_dir, float3 box_min, float3 box_max, float t_max)
{
    const float3 box_min_rel = box_min - ray_origin;
    const float3 box_max_rel = box_max - ray_origin;

    const float3 t_plane_min = box_min_rel * ray_inv_dir;
    const float3 t_plane_max = box_max_rel * ray_inv_dir;

    float3 min_interval, max_interval;

    min_interval.x = ray_inv_dir.x >= 0.0f ? t_plane_min.x : t_plane_max.x;
    max_interval.x = ray_inv_dir.x >= 0.0f ? t_plane_max.x : t_plane_min.x;

    min_interval.y = ray_inv_dir.y >= 0.0f ? t_plane_min.y : t_plane_max.y;
    max_interval.y = ray_inv_dir.y >= 0.0f ? t_plane_max.y : t_plane_min.y;

    min_interval.z = ray_inv_dir.z >= 0.0f ? t_plane_min.z : t_plane_max.z;
    max_interval.z = ray_inv_dir.z >= 0.0f ? t_plane_max.z : t_plane_min.z;

    const float min_of_intervals = max(max(min_interval.x, min_interval.y), min_interval.z);
    const float max_of_intervals = min(min(max_interval.x, max_interval.y), max_interval.z);

    const float min_t = max(min_of_intervals, 0.0f);
    const float max_t = min(max_of_intervals, t_max);

    return float2(min_t, max_t);
}

#endif  // AABB_H