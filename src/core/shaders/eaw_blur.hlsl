
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
RWTexture2D<float4> g_variance : register(u2);
RWTexture2D<float4> g_output_color : register(u3);
RWTexture2D<float4> g_output_variance : register(u4);


float CalculateNormalWeight(float3 n0, float3 n1)
{
    return pow(max(dot(n0, n1), 0.0), 128.f);
}

float CalculateDepthWeight(float d0, float d1)
{
    return Gaussian(d0, d1, 3.f);
}

float CalculateLumaWeight(float l0, float l1, float var)
{
    float sigma = 20.f * (sqrt(var)) + 1e-5f;
    return Gaussian(l0, l1, sigma);
}

float CalculateObjectIDWeight(uint id0, uint id1)
{
    return (id0 == id1) ? 1.f : 0.f;
}

float SampleVariance(in uint2 xy, bool resolve)
{
    if (resolve)
    {
        float4 moments = g_variance[xy];// / g_variance[xy].w;
        return abs(moments.y - moments.x * moments.x);
    }
    else
    {
        return g_color[xy].w;
    }
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void Blur(in uint2 gidx: SV_DispatchThreadID,
          in uint2 lidx: SV_GroupThreadID,
          in uint2 bidx: SV_GroupID)
{
    if (gidx.x >= g_constants.width || gidx.y >= g_constants.height)
        return;

    bool resolve_variance = g_constants.stride == 1;

    float3 filtered = 0.f;
    float filtered_var = 0.f;
    float tw = 0.f;
    float tw_var = 0.f;

    float4 gc = g_gbuffer.Load(int3(gidx, 0));
    float3 nc = OctDecode(gc.xy);
    float  nd = gc.w;
    uint   id = uint(gc.z);
    float3 cc = g_color[gidx].xyz;

    if (nd < 1e-5f)
    {
        g_output_color[gidx] = float4(g_color[gidx].xyz, 0.f);
        return;
    }

    const float kEAWWeights[5] = {1.f / 16.f, 1.f / 4.f, 3.f / 8.f, 1.f / 4.f, 1.f / 16.f};

    const int kRadius = 2;
    for (int dy = -kRadius; dy <= kRadius; ++dy)
    {
        for (int dx = -kRadius; dx <= kRadius; ++dx)
        {
            int2 xy = int2(gidx) + int2(dx * g_constants.stride, dy * g_constants.stride);

            if (any(xy < 0) || any(xy >= int2(g_constants.width, g_constants.height)))
                continue;

            float4 g = g_gbuffer.Load(int3(xy, 0));
            float3 n = OctDecode(g.xy);
            float  d = g.w;
            uint   i = uint(g.z);

            if (d < 1e-5f) continue;

            float h_weight = 1.f;//kEAWWeights[dy + kRadius] * kEAWWeights[dx + kRadius];
            float weight = CalculateDepthWeight(nd, d) * CalculateNormalWeight(nc, n) * CalculateLumaWeight(luminance(cc), luminance(g_color[xy].xyz), SampleVariance(xy, resolve_variance));

            filtered += weight * h_weight * g_color[xy].xyz;
            filtered_var += h_weight * h_weight * weight * weight * SampleVariance(xy, resolve_variance);
            tw += weight * h_weight;
            tw_var += h_weight * h_weight * weight * weight;
        }
    }

    

    g_output_color[gidx] = float4(tw < 1e-5 ? 0.f : (filtered / tw), tw_var < 1e-5 ? 0.f : (filtered_var / tw_var));
}