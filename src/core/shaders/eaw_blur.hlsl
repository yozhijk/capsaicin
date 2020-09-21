
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
RWTexture2D<float4> g_moments : register(u2);
RWTexture2D<float4> g_output_color : register(u3);

float SampleVariance(in uint2 xy)
{
    return g_color[xy].w;
}

float3 SampleColorRemoveFireflies(in uint2 xy)
{
    return min(g_color[xy].xyz, 10.f);
}

float GetHistoryLength(in uint2 xy)
{
    return g_moments[xy].w;
}

float2 GetMoments(in uint2 xy)
{
    return g_moments[xy].xy;
}

// Edge-avoding a-trous wavelet blur with edge stoping functions, part of
// "Spatiotemporal Variance-Guided Filtering: Real-Time Reconstruction for Path-Traced Global Illumination" Schied et Al
// https://research.nvidia.com/sites/default/files/pubs/2017-07_Spatiotemporal-Variance-Guided-Filtering%3A//svgf_preprint.pdf
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void Blur(in uint2 gidx: SV_DispatchThreadID,
          in uint2 lidx: SV_GroupThreadID,
          in uint2 bidx: SV_GroupID)
{
    if (gidx.x >= g_constants.width || gidx.y >= g_constants.height)
        return;

    // TODO: remove this dirty hack.
    float   history_length = GetHistoryLength(gidx);
    float3  filtered_color = 0.f;
    float   filtered_variance = 0.f;
    float   total_weight = 0.f;

    float4  center_g = g_gbuffer.Load(int3(gidx, 0));
    float3  center_n = OctDecode(center_g.xy);
    float   center_d = center_g.w;
    float3  center_color = SampleColorRemoveFireflies(gidx);
    float   center_variance = 0.f;

#ifdef USE_VARIANCE
    center_variance = SampleVariance(gidx);
#endif

    // Handle background.
    if (center_d < 1e-5f)
    {
        g_output_color[gidx] = float4(center_color, center_variance);
        return;
    }

    // EAW filter weights.
    const float kWeights[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };
    const float s_depth = center_d * g_constants.stride * g_constants.depth_sigma;
    const float s_normal = g_constants.normal_sigma;
    const float s_luma = g_constants.luma_sigma * sqrt(max(0.f, center_variance + EPS));

    // Filter neighbourhood.
    const int kRadius = 2;
    for (int dy = -kRadius; dy <= kRadius; ++dy)
    {
        for (int dx = -kRadius; dx <= kRadius; ++dx)
        {
            int2 xy = int2(gidx) + int2(dx * g_constants.stride, dy * g_constants.stride);

            if (any(xy < 0) || any(xy >= int2(g_constants.width, g_constants.height)))
            {
                continue;
            }

            float3 c = SampleColorRemoveFireflies(xy);
            float4 g = g_gbuffer.Load(int3(xy, 0));
            float3 n = OctDecode(g.xy);
            float  d = g.w;
            float  v = SampleVariance(xy);

            // Skip background.
            if (d < 1e-5f)
            {
                continue;
            }

            // Calculate luma weight.
            float luma_weight = 1.f;
            // Calculate EAW weight.
            float h_weight = 1.f;
#ifdef USE_VARIANCE
            luma_weight = CalculateLumaWeight(luminance(center_color), luminance(c), s_luma);
            // Calculate EAW weight.
            h_weight = kWeights[abs(dx)] * kWeights[abs(dy)];
#endif
            // Сalculate depth and normal weight
            float weight = CalculateNormalWeight(center_n, n, s_normal) * 
                           CalculateDepthWeight(center_d, d, s_depth * length(float2(dx, dy)));

            // Filter color and variance.
            filtered_color += weight * h_weight * luma_weight * c;
            total_weight += weight * h_weight * luma_weight;

#ifdef USE_VARIANCE
            filtered_variance += h_weight * h_weight * weight * weight * luma_weight * luma_weight * SampleVariance(xy);
#endif
        }
    }

    // Output filtered color and variance.
    center_color = (total_weight < EPS) ? center_color : (filtered_color / total_weight);
    center_variance =  (total_weight < EPS) ? center_variance : (filtered_variance / (total_weight * total_weight));
    g_output_color[gidx] = float4(center_color, center_variance);
}

#define SPATIAL_VARIANCE_ESTIMATE_THRESHOLD 8

// This pass effectively blurs disoccluded areas and recalculates variance based on spatial estimate there.
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void BlurDisocclusion(in uint2 gidx: SV_DispatchThreadID,
                      in uint2 lidx: SV_GroupThreadID,
                      in uint2 bidx: SV_GroupID)
{
    if (gidx.x >= g_constants.width || gidx.y >= g_constants.height)
        return;

    float   history_length = GetHistoryLength(gidx);
    float3  filtered_color = 0.f;
    float2  filtered_moments = 0.f;
    float   total_weight = 0.f;

    float4  center_g = g_gbuffer.Load(int3(gidx, 0));
    float3  center_n = OctDecode(center_g.xy);
    float   center_d = center_g.w;
    float3  center_color = SampleColorRemoveFireflies(gidx);
    float2  center_m = 0.f;
    float   center_variance = 0.f;

#ifdef USE_VARIANCE
    center_m = GetMoments(gidx);
    center_variance = SampleVariance(gidx);
#endif

    if (center_d < 1e-5f || history_length >= SPATIAL_VARIANCE_ESTIMATE_THRESHOLD)
    {
        g_output_color[gidx] = float4(center_color, center_variance);
        return;
    }

    // EAW filter weights.
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

            float3 c = SampleColorRemoveFireflies(xy);
            float4 g = g_gbuffer.Load(int3(xy, 0));
            float3 n = OctDecode(g.xy);
            float  d = g.w;
            float2 m = GetMoments(xy);

            // Skip background.
            if (d < 1e-5f)
            {
                continue;
            }

            // Сalculate depth and normal weight
            float weight = CalculateNormalWeight(center_n, n, s_normal) * 
                           CalculateDepthWeight(center_d, d, s_depth * length(float2(dx, dy))) * 
                           CalculateLumaWeight(luminance(center_color), luminance(c), s_luma);

            // Filter color and variance.
            filtered_color += weight * c;
            filtered_moments += weight * m;
            total_weight += weight;
        }
    }

    // Output filtered color and variance.
    center_color = (total_weight < EPS) ? center_color : (filtered_color / total_weight);
    center_m =  (total_weight < EPS) ? 0.f : (filtered_moments / total_weight);

    float variance_boost = SPATIAL_VARIANCE_ESTIMATE_THRESHOLD / history_length;

    center_variance = variance_boost * abs(center_m.y - center_m.x * center_m.x);
    g_output_color[gidx] = float4(center_color, center_variance);
}