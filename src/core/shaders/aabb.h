#ifndef AABB_H
#define AABB_H

#include "math_functions.h"

struct AABB
{
    float3 pmin;
    float3 pmax;
};

// AABB center.
float3 CalculateAABBCenter(in AABB aabb)
{
    return 0.5f * (aabb.pmin + aabb.pmax);
}

// AABB extent
float3 CalculateAABBExtent(in AABB aabb)
{
    return aabb.pmax - aabb.pmin;
}

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

#endif // AABB_H