
#include "math_functions.h"
#include "eaw_edge_stopping.h"

#define TILE_SIZE 8

struct Constants
{
    uint width;
    uint height;
    uint frame_count;
    uint stride;

    float normal_sigma;
    float depth_sigma;
    float luma_sigma;
    float padding;
};

ConstantBuffer<Constants> g_constants : register(b0);
RWTexture2D<float4> g_color : register(u0);
RWTexture2D<float4> g_gbuffer : register(u1);
RWTexture2D<float4> g_output_color : register(u2);
Texture2D<float4> g_blue_noise: register(t0);

#include "sampling.h"

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void Gather(in uint2 gidx: SV_DispatchThreadID,
            in uint2 lidx: SV_GroupThreadID,
            in uint2 bidx: SV_GroupID)
{
    if (gidx.x >= g_constants.width || gidx.y >= g_constants.height)
        return;

#ifdef UPSCALE2X
    // We are interleaving in 2x2 fullres regions.
    uint2 sp_offset = uint2((g_constants.frame_count % 4) / 2,
                            (g_constants.frame_count % 4) % 2);

    uint2 fullres_dims = uint2(g_constants.width, g_constants.height) << 1;
    uint2 fullres_center_xy = (gidx << 1) + sp_offset;
#else
    uint2 fullres_dims = uint2(g_constants.width, g_constants.height);
    uint2 fullres_center_xy = gidx;
#endif

    float3 filtered_color = 0.f;
    float total_weight = 0.f;

    float4 center_g = g_gbuffer.Load(int3(fullres_center_xy, 0));
    float3 center_n = OctDecode(center_g.xy);
    float  center_d = center_g.w;
    float3 center_color = g_color[gidx].xyz;
    
    // Handle background.
    if (center_d < 1e-5f)
    {
        g_output_color[gidx] = float4(center_color, 0.f);
        return;
    }

    const float s_depth = center_d * g_constants.depth_sigma;
    const float s_normal = g_constants.normal_sigma;
    const float s_luma = g_constants.luma_sigma;

    // Filter neighbourhood.
    const int kRadius = 3;
    for (int dy = -kRadius; dy <= kRadius; ++dy)
    {
        for (int dx = -kRadius; dx <= kRadius; ++dx)
        {
            int2 xy = int2(gidx) + int2(dx, dy);

            if (any(xy < 0) || any(xy >= int2(g_constants.width, g_constants.height)))
            {
                continue;
            }

#ifdef UPSCALE2X
            uint2 fullres_xy = (xy << 1) + sp_offset;
#else
            uint2 fullres_xy = xy;
#endif

            float3 c = g_color[xy].xyz;
            float4 g = g_gbuffer.Load(int3(fullres_xy, 0));
            float3 n = OctDecode(g.xy);
            float  d = g.w;

            // Skip background.
            if (d < 1e-5f)
            {
                continue;
            }

            float weight = CalculateNormalWeight(center_n, n, s_normal) *
                           CalculateDepthWeight(center_d, d, s_depth * length(float2(dx, dy))) * 
                           CalculateLumaWeight(luminance(center_color), luminance(c), s_luma);

            filtered_color += weight * c;
            total_weight += weight;
        }
    }

    // Output filtered color and variance.
    center_color = (total_weight < EPS) ? center_color : (filtered_color / total_weight);
    g_output_color[gidx] = float4(center_color, 1.f);
}