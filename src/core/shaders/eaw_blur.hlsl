
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
    return pow(max(dot(n0, n1), 0.0), 128.f);
}

float CalculateDepthWeight(float d0, float d1)
{
    return Gaussian(d0, d1, 3.f);
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void Blur(in uint2 gidx: SV_DispatchThreadID,
          in uint2 lidx: SV_GroupThreadID,
          in uint2 bidx: SV_GroupID)
{
    if (gidx.x >= g_constants.width || gidx.y >= g_constants.height)
        return;

    float3 filtered = 0.f;
    float tw = 0.f;

    float3 nc = g_gbuffer.Load(int3(gidx, 0)).xyz;
    float  nd = g_gbuffer.Load(int3(gidx, 0)).w;

    if (nd < 1e-5f)
    {
        g_output_color[gidx] = g_color[gidx];
        return;
    }

    const int kRadius = 2;
    for (int dy = -kRadius; dy <= kRadius; ++dy)
    {
        for (int dx = -kRadius; dx <= kRadius; ++dx)
        {
            int2 xy = int2(gidx) + int2(dx * g_constants.stride, dy * g_constants.stride);

            if (any(xy < 0) || any(xy >= int2(g_constants.width, g_constants.height)))
                continue;

            float3 n = g_gbuffer.Load(int3(xy, 0)).xyz;
            float  d = g_gbuffer.Load(int3(xy, 0)).w;

            if (d < 1e-5f) continue;

            float weight = CalculateNormalWeight(nc, n) * CalculateDepthWeight(nd, d);

            filtered += weight * g_color[xy];
            tw += weight;
        }
    }

    g_output_color[gidx] = float4(filtered / tw, 1.f);
}