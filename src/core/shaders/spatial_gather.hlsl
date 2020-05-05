
#include "math_functions.h"
#define TILE_SIZE 8

struct Constants
{
    uint width;
    uint height;
    uint frame_count;
    uint stride;
};

ConstantBuffer<Constants> g_constants : register(b0);
RWTexture2D<float4> g_color : register(u0);
RWTexture2D<float4> g_gbuffer : register(u1);
RWTexture2D<float4> g_output_color : register(u2);

float CalculateNormalWeight(float3 n0, float3 n1)
{
    const float kNormalSigma = 128.f;
    return pow(max(dot(n0, n1), 0.0), kNormalSigma);
}

float CalculateDepthWeight(float d0, float d1)
{
    const float kDepthSigma = 5.f;
    return Gaussian(d0, d1, kDepthSigma);
}


[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void Gather(in uint2 gidx: SV_DispatchThreadID,
            in uint2 lidx: SV_GroupThreadID,
            in uint2 bidx: SV_GroupID)
{
    if (gidx.x >= g_constants.width || gidx.y >= g_constants.height)
        return;

    float3 filtered_color = 0.f;
    float total_weight = 0.f;

    float4 center_g = g_gbuffer.Load(int3(gidx, 0));
    float3 center_n = OctDecode(center_g.xy);
    float  center_d = center_g.w;
    float3 center_color = g_color[gidx].xyz;

    // Handle background.
    if (center_d < 1e-5f)
    {
        g_output_color[gidx] = float4(center_color, 0.f);
        return;
    }

    // Filter neighbourhood.
    const int kRadius = 3;
    for (int dy = -kRadius; dy <= kRadius; ++dy)
    {
        for (int dx = -kRadius; dx <= kRadius; ++dx)
        {
            int2 xy = int2(gidx) + int2(dx * g_constants.stride, dy * g_constants.stride);

            if (any(xy < 0) || any(xy >= int2(g_constants.width, g_constants.height)))
            {
                continue;
            }

            float3 c = g_color[xy].xyz;
            float4 g = g_gbuffer.Load(int3(xy, 0));
            float3 n = OctDecode(g.xy);
            float  d = g.w;

            // Skip background.
            if (d < 1e-5f || g.z != center_g.z)
            {
                continue;
            }

            float weight = CalculateNormalWeight(center_n, n);

            filtered_color += weight * c;
            total_weight += weight;
        }
    }

    // Output filtered color and variance.
    center_color = (total_weight < EPS) ? 0.f : (filtered_color / total_weight);
    g_output_color[gidx] = float4(center_color, 1.f);
}