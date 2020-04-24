#include "camera.h"
#include "sampling.h"
#include "lighting.h"
#include "shading.h"
#include "scene.h"

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
    float3 color;
    uint recursion_depth;

    float3 throughput;
};

struct ShadowRayPayload
{
    bool hit;
};

ConstantBuffer<Constants> g_constants : register(b0);
ConstantBuffer<Camera> g_camera : register(b1);
RaytracingAccelerationStructure g_scene : register(t0);
Texture2D<float4> g_blue_noise: register(t1);
SamplerState g_sampler: register(s0);
Texture2D<float4> g_textures[]: register(t2);
RWBuffer<uint> g_index_buffer : register(u0);
RWBuffer<float> g_vertex_buffer : register(u1);
RWBuffer<float> g_normal_buffer : register(u2);
RWBuffer<float2> g_texcoord_buffer : register(u3);
RWStructuredBuffer<Mesh> g_mesh_buffer : register(u4);
RWTexture2D<float4> g_output_color_direct : register(u5);
RWTexture2D<float4> g_output_color_indirect : register(u6);
RWTexture2D<float4> g_output_gbuffer : register(u7);
RWTexture2D<float4> g_output_gbuffer_albedo: register(u8);

float3 CalculateDirectIllumination(float3 v, float3 n, float3 kd)
{
    LightSample ss = DirectionalLight_Sample(g_constants.frame_count);

    ShadowRayPayload payload;
    payload.hit = true;

    RayDesc shadow_ray;
    shadow_ray.Direction = ss.direction;
    shadow_ray.Origin = v;
    shadow_ray.TMin = 0.001f;
    shadow_ray.TMax = ss.distance;

    TraceRay(g_scene, RAY_FLAG_FORCE_OPAQUE , ~0, 1, 0, 1, shadow_ray, payload);

    if (payload.hit) return 0.f;

    return ss.intensity * kd * Lambert_Evaluate(n, ss.direction) * max(dot(n, ss.direction), 0.0);
}

RayDesc CreatePrimaryRay(in uint2 xy, in uint2 dim)
{
    float2 s = Sample2D_BlueNoise4x4(g_blue_noise, xy, g_constants.frame_count);

    // Calculate [0..1] image plane sample
    float2 img_sample = (float2(xy) + s) / float2(dim);

    // Transform into [-0.5, 0.5]
    float2 h_sample = img_sample - float2(0.5f, 0.5f);
    // Transform into [-dim/2, dim/2]
    float2 c_sample = h_sample * g_camera.sensor_size;

    RayDesc my_ray;
    // Calculate direction to image plane
    my_ray.Direction = normalize(g_camera.focal_length * g_camera.forward + c_sample.x * g_camera.right + c_sample.y * g_camera.up);
    // Origin == camera position + nearz * d
    my_ray.Origin = g_camera.position;
    my_ray.TMin = 0.f;
    my_ray.TMax = 1e6f;
    return my_ray;
}

[shader("raygeneration")]
void TraceVisibility()
{
    RayDesc ray = CreatePrimaryRay(DispatchRaysIndex(), DispatchRaysDimensions());

    RayPayload payload;
    payload.color = 0.f;
    payload.throughput = 1.f;
    payload.recursion_depth = 0;
    TraceRay(g_scene, RAY_FLAG_FORCE_OPAQUE , ~0, 0, 0, 0, ray, payload);

    uint2 output_xy = DispatchRaysIndex();
    g_output_color_indirect[output_xy] = float4(payload.color, 1.f);
}

[shader("anyhit")]
void ShadowHit(inout ShadowRayPayload payload, in MyAttributes attr)
{
    payload.hit = true;
}

[shader("closesthit")]
void Hit(inout RayPayload payload, in MyAttributes attr)
{
    uint2 xy = DispatchRaysIndex();
    uint prim_index = PrimitiveIndex();
    uint instance_index = InstanceID();

    Mesh mesh = g_mesh_buffer[instance_index];
    uint io = mesh.first_index_offset;
    uint i0 = g_index_buffer[io + 3 * prim_index + 0];
    uint i1 = g_index_buffer[io + 3 * prim_index + 1];
    uint i2 = g_index_buffer[io + 3 * prim_index + 2];

    uint vo = mesh.first_vertex_offset * 3;
    float3 v0 = float3(g_vertex_buffer[vo + 3 * i0], g_vertex_buffer[vo + 3 * i0 + 1], g_vertex_buffer[vo + 3 * i0 + 2]);
    float3 v1 = float3(g_vertex_buffer[vo + 3 * i1], g_vertex_buffer[vo + 3 * i1 + 1], g_vertex_buffer[vo + 3 * i1 + 2]);
    float3 v2 = float3(g_vertex_buffer[vo + 3 * i2], g_vertex_buffer[vo + 3 * i2 + 1], g_vertex_buffer[vo + 3 * i2 + 2]);

    float3 n0 = float3(g_normal_buffer[vo + 3 * i0], g_normal_buffer[vo + 3 * i0 + 1], g_normal_buffer[vo + 3 * i0 + 2]);
    float3 n1 = float3(g_normal_buffer[vo + 3 * i1], g_normal_buffer[vo + 3 * i1 + 1], g_normal_buffer[vo + 3 * i1 + 2]);
    float3 n2 = float3(g_normal_buffer[vo + 3 * i2], g_normal_buffer[vo + 3 * i2 + 1], g_normal_buffer[vo + 3 * i2 + 2]);

    vo = mesh.first_vertex_offset;
    float2 t0 = g_texcoord_buffer[vo + i0];
    float2 t1 = g_texcoord_buffer[vo + i1];
    float2 t2 = g_texcoord_buffer[vo + i2];

    float2 uv = attr.barycentrics.xy;
    float3 n = normalize(n0 * (1.f - uv.x - uv.y) + n1 * uv.x + n2 * uv.y);
    float3 v = v0 * (1.f - uv.x - uv.y) + v1 * uv.x + v2 * uv.y;
    float2 t = t0 * (1.f - uv.x - uv.y) + t1 * uv.x + t2 * uv.y;

    t.y = 1.f - t.y;
    float3 kd = (mesh.texture_index == ~0u) ? 1.f : g_textures[NonUniformResourceIndex(mesh.texture_index)].SampleLevel(g_sampler, t, 0);
    kd = pow(kd, 2.2f);

    // Output GBuffer data for primary hits.
    if (payload.recursion_depth == 0)
    {
        // Revert normal if needed.
        float3 view = normalize(g_camera.position - v);
        if (dot(view, n) < 0.f) n = -n;
        g_output_color_direct[xy] = float4(CalculateDirectIllumination(v, n, kd), 1.f);
        g_output_gbuffer[xy] = float4(n, length(g_camera.position - v));
        g_output_gbuffer_albedo[xy] = float4(kd, mesh.texture_index);
    }
    else
    {
        payload.color += payload.throughput * CalculateDirectIllumination(v, n, kd);
    }

    // For all the bounces except the last one, cast extension ray.
    if (payload.recursion_depth < 2)
    {
        // Add indirect.
        float2 s = Sample2D_BlueNoise4x4(g_blue_noise, xy, g_constants.frame_count);

        BrdfSample ss = Lambert_Sample(s, n);

        RayDesc next_ray;
        next_ray.Direction = ss.direction;
        next_ray.Origin = v;
        next_ray.TMin = 0.001f;
        next_ray.TMax = ss.distance;

        if (ss.pdf == 0.f)
        {
            return;
        }

        payload.throughput *= (ss.brdf * max(0.f, dot(n, ss.direction)) / ss.pdf);

        if (payload.recursion_depth != 0)
        {
            payload.throughput *= kd;
        }

        // Trace indirect ray.
        ++payload.recursion_depth;
        TraceRay(g_scene, RAY_FLAG_FORCE_OPAQUE , ~0, 0, 0, 0, next_ray, payload);
    }
}

[shader("miss")]
void ShadowMiss(inout ShadowRayPayload payload)
{
    payload.hit = false;
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    uint2 xy = DispatchRaysIndex().xy;

    if (payload.recursion_depth == 0)
    {
        g_output_gbuffer[xy] = 0.f;
        g_output_gbuffer_albedo[xy] = 0.f;
        g_output_color_direct[xy] = float4(0.7f, 0.7f, 0.85f, 1.f);
    }
    else
    {
        payload.color += payload.throughput * float4(0.7f, 0.7f, 0.85f, 1.f);
    }
}