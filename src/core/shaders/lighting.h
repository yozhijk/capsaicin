// clang-format off
#ifndef LIGHTING_H
#define LIGHTING_H

#include "shading.h"

struct ShadowRayPayload
{
    bool hit;
};

struct LightSample
{
    float3 direction;
    float3 intensity;
    float  pdf;
    float  distance;
};

LightSample DirectionalLight_Sample(uint count)
{
    float t  = 2.f * 3.14 * float(count % 4096) / 4096.f;
    float ly = 100.f;
    float lx = 40.f * sin(t);
    float lz = 40.f * cos(t);

    LightSample ls;
    ls.direction = normalize(float3(lx, ly, lz));
    ls.intensity = 1.f * (3.f * float3(14.f, 12.f, 10.f) + float3(0.f, 0.f, 2.f + 2.f * cos(t)));
    ls.pdf       = 1.f;
    ls.distance  = 100000.f;
    return ls;
}

float3 CalculateDirectIllumination(in float3 v, in float3 n, in float3 kd, in uint shadow_hitgroup_index)
{
    LightSample ss = DirectionalLight_Sample(g_constants.frame_count);

    ShadowRayPayload payload;
    payload.hit = true;

    RayDesc shadow_ray;
    shadow_ray.Direction = ss.direction;
    shadow_ray.Origin    = v;
    shadow_ray.TMin      = 0.0001f;
    shadow_ray.TMax      = ss.distance;

    TraceRay(g_scene,
             RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
             ~0,
             shadow_hitgroup_index, // Ray contribution to hitgroup index
             0,
             shadow_hitgroup_index, // Miss shader index
             shadow_ray,
             payload);

    if (payload.hit)
        return 0.f;

    return ss.intensity * kd * Lambert_Evaluate(n, ss.direction) * max(0.f, dot(n, ss.direction));
}

[shader("anyhit")]
void ShadowAnyHit(inout ShadowRayPayload payload, in MyAttributes attr)
{
    payload.hit = true;
}

[shader("miss")]
void ShadowMiss(inout ShadowRayPayload payload)
{
    payload.hit = false;
}

#endif
// clang-format on