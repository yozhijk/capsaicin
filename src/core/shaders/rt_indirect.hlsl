// clang-format off
#include "data_payload.h"

typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct Constants
{
    uint width;
    uint height;
    uint frame_count;
    uint num_bounces;
};

struct RayPayload
{
    float2 uv;
    uint instance_index;
    uint prim_index;
};

ConstantBuffer<Constants> g_constants : register(b0);
ConstantBuffer<Camera> g_camera : register(b1);
ConstantBuffer<Camera> g_prev_camera : register(b2);
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
RWTexture2D<float4> g_color_history : register(u6);
RWTexture2D<float4> g_prev_gbuffer_normal_depth : register(u7);
RWTexture2D<float4> g_output_indirect : register(u8);

#include "lighting.h"
#include "math_functions.h"
#include "sampling.h"
#include "scene.h"
#include "shading.h"
#include "camera.h"

[shader("raygeneration")]
void CalculateIndirectDiffuseLighting()
{
    // These are half-res indices.
    uint2 xy = DispatchRaysIndex();
    uint2 dims = DispatchRaysDimensions();


#ifdef LOWRES_INDIRECT
    // We are interleaving in 2x2 fullres regions.
    uint2 sp_offset = uint2((g_constants.frame_count % 4) / 2,
                            (g_constants.frame_count % 4) % 2);

    uint2 fullres_dims = dims << 1;
    uint2 fullres_xy = (xy << 1) + sp_offset;
#else
    uint2 fullres_dims = dims;
    uint2 fullres_xy = xy;
#endif

    // Reconstruct primary ray to determine backface hits.
    RayDesc ray = CreatePrimaryRay(fullres_xy, fullres_dims);

    // Load GBuffer information and decode.
    float4 g            = g_gbuffer_geo[fullres_xy];
    float2 uv           = g.xy;
    uint instance_index = asuint(g.z);
    uint prim_index     = asuint(g.w);

    // If this is not a valid primary hit, output background color.
    if (instance_index == INVALID_ID)
    {
        g_output_indirect[xy] = float4(0.f, 0.f, 0.f, 1.f);
        return;
    }

    //
    float3 color = 0.f;
    float3 throughput = 1.f;

    // Prepare indirect ray payload.
    RayPayload payload;
    payload.instance_index = instance_index;
    payload.prim_index     = prim_index;
    payload.uv             = uv;

    for (uint bounce = 0; bounce <= g_constants.num_bounces; ++bounce)
    {
        // The ray missed geometry.
        if (payload.instance_index == INVALID_ID)
        {
            // Add IBL contribution.
            color += throughput * float4(0.7f, 0.7f, 0.85f, 1.f);
            break;
        }

        float3 p, n;
        float2 tx;
        InterpolateAttributes(payload.instance_index, payload.prim_index, payload.uv, p, n, tx);

        float3 kd = GetMaterial(payload.instance_index, tx);

        // If no contribution possible, bail out.
        if (all(kd < 1e-5f))
        {
            break;
        }

        // Add lighting.
        if (bounce != 0)
        {
            // GBuffer feedback reads shaded results from previous frame GBuffer if available,
            // essentially achieving multiple bounces at the cost of one.
#ifdef GBUFFER_FEEDBACK
            // If this point already present in history - return shaded result.
            float2 prev_frame_uv = CalculateImagePlaneUV(g_prev_camera, p);
            bool disocclusion = any(prev_frame_uv < 0.f) || any(prev_frame_uv > 1.f);

            float2 prev_frame_xy = UVtoXY(prev_frame_uv, fullres_dims);
            float4 prev_gbuffer_data = g_prev_gbuffer_normal_depth.Load(int3(prev_frame_xy, 0));

            // Calculate depth from previous frame camera.
            float current_depth = length(p - g_prev_camera.position);
            float prev_depth = prev_gbuffer_data.w;

            disocclusion = disocclusion || (abs(prev_depth - current_depth) / current_depth > 0.05);
            if (disocclusion)
            {
#endif
                // In this config shadow hitgoup is the second one after indirect hit, so index 1.
                const uint shadow_hitgroup_index = 1;
                color += throughput * CalculateDirectIllumination(p, n, kd, shadow_hitgroup_index);
#ifdef GBUFFER_FEEDBACK
            }
            else
            {
                // Fetch history color.
                color += throughput * SampleBilinear(g_color_history, prev_frame_uv, fullres_dims);
                break;
            }
#endif
        }

        // Generate random sample for BRDF sampling.
        float2 s = Sample2D_BlueNoise4x4(g_blue_noise, fullres_xy, g_constants.frame_count * 25 + bounce);

        // Sample Lambertian BRDF.
        BrdfSample ss = Lambert_Sample(s, n);

        ray.Direction = ss.direction;
        ray.Origin = p;
        ray.TMin = 0.0001f;
        ray.TMax = ss.distance;

        // If probability is too low, bail out.
        if (ss.pdf < 1e-5f)
        {
            break;
        }

        throughput *= (ss.brdf * max(dot(n, ss.direction), 0.f) / ss.pdf);

        if (bounce != 0)
        {
            throughput *= kd;
        }

        // Trace indirect ray.
        TraceRay(g_scene, RAY_FLAG_FORCE_OPAQUE, ~0, 0, 0, 0, ray, payload);
    }

    g_output_indirect[xy] = float4(color, 1.f);
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in MyAttributes attr)
{
    payload.uv              = attr.barycentrics.xy;
    payload.instance_index  = InstanceID();
    payload.prim_index      = PrimitiveIndex();
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.instance_index = INVALID_ID;
    payload.prim_index     = INVALID_ID;
}
// clang-format on