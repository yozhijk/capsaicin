// clang-format off
#include "data_payload.h"

typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct Constants
{
    uint width;
    uint height;
    uint frame_count;
    uint padding;
};

struct RayPayload
{
    float2  uv;
    uint    instance_index;
    uint    prim_index;
};

ConstantBuffer<Constants>       g_constants : register(b0);
ConstantBuffer<Camera>          g_camera : register(b1);
RaytracingAccelerationStructure g_scene : register(t0);
Texture2D<float4>               g_blue_noise : register(t1);
SamplerState                    g_sampler : register(s0);
RWTexture2D<float4>             g_output_gbuffer_geo : register(u0);

#include "camera.h"
#include "math_functions.h"
#include "sampling.h"


// Generate and trace primary camera rays.
[shader("raygeneration")]
void TracePrimaryRays()
{
    uint2 xy = DispatchRaysIndex();
    RayDesc ray = CreatePrimaryRay(xy, DispatchRaysDimensions());

    RayPayload payload;
    payload.uv              = 0.f;
    payload.instance_index  = INVALID_ID;
    payload.prim_index      = INVALID_ID;
    TraceRay(g_scene, RAY_FLAG_FORCE_OPAQUE, ~0, 0, 0, 0, ray, payload);

    g_output_gbuffer_geo[xy] = float4(payload.uv,
                                      asfloat(payload.instance_index),
                                      asfloat(payload.prim_index));
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in MyAttributes attr)
{
    payload.uv              = attr.barycentrics.xy;
    payload.instance_index  = InstanceID();
    payload.prim_index      = PrimitiveIndex();
}
// clang-format on
