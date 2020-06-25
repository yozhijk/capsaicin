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

ConstantBuffer<Constants> g_constants : register(b0);
ConstantBuffer<Camera> g_camera : register(b1);
RaytracingAccelerationStructure g_scene : register(t0);
Texture2D<float4> g_blue_noise : register(t1);
SamplerState g_sampler : register(s0);
Texture2D<float4> g_textures[] : register(t2);
RWBuffer<uint> g_index_buffer : register(u0);
RWBuffer<float> g_vertex_buffer : register(u1);
RWBuffer<float> g_normal_buffer : register(u2);
RWBuffer<float2> g_texcoord_buffer : register(u3);
RWStructuredBuffer<Mesh> g_mesh_buffer : register(u4);
RWTexture2D<float4> g_gbuffer_geo : register(u5);
RWTexture2D<float4> g_output_direct : register(u6);
RWTexture2D<float4> g_output_albedo: register(u7);
RWTexture2D<float4> g_output_normal_depth: register(u8);

#include "camera.h"
#include "lighting.h"
#include "math_functions.h"
#include "sampling.h"
#include "scene.h"
#include "shading.h"

[shader("raygeneration")]
void CalculateDirectLighting()
{
    uint2 xy = DispatchRaysIndex();
    uint2 dims = DispatchRaysDimensions();

    // Reconstruct primary ray to determine backface hits.
    RayDesc primary_ray = CreatePrimaryRay(xy, dims); 

    // Load GBuffer information and decode.
    float4 g            = g_gbuffer_geo[xy];
    float2 uv           = g.xy;
    uint instance_index = asuint(g.z);
    uint prim_index     = asuint(g.w);

    // If this is not a valid primary hit, output background color.
    if (instance_index == INVALID_ID)
    {
        g_output_direct[xy]         = float4(0.7f, 0.7f, 0.85f, 1.f);
        g_output_albedo[xy]         = 1.f;
        g_output_normal_depth[xy]   = 0.f;
        return;
    }

    // Otherwise reconstruct world space position, normal and texture coordinates.
    float3 p, n;
    float2 tx;
    InterpolateAttributes(instance_index, prim_index, uv, p, n, tx);

    float3 kd = GetMaterial(instance_index, tx);

    if (all(kd < 1e-5f) || dot(-primary_ray.Direction, n) <= 0.f)
    {
        g_output_direct[xy]         = float4(0.f, 0.f, 0.f, 1.f);
        g_output_albedo[xy]         = 0.f;
        g_output_normal_depth[xy]   = 0.f;
        return;
    }

    // In this config shadow hitgoup is the only one, hence index 0.
    const uint shadow_hitgroup_index = 0;
    float3 di = CalculateDirectIllumination(p, n, kd, shadow_hitgroup_index);

    g_output_direct[xy]         = float4(di, 1.f);
    g_output_albedo[xy]         = float4(kd, 1.f);
    g_output_normal_depth[xy]   = float4(OctEncode(n), instance_index, length(g_camera.position - p));
}

// clang-format on
