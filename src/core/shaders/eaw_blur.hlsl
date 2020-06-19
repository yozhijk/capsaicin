
#include "math_functions.h"
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

// Edge stopping functions.
float CalculateNormalWeight(float3 n0, float3 n1)
{
    return pow(max(dot(n0, n1), 0.0), g_constants.normal_sigma);
}

float CalculateDepthWeight(float d0, float d1)
{
    float v = abs(d0 - d1) / (g_constants.depth_sigma * d0);
    return exp(-v);
}

float CalculateLumaWeight(float l0, float l1, float var)
{
    float v = abs(l0 - l1) / ((g_constants.luma_sigma * sqrt(max(0.f, var))) + 1e-5);
    return exp(-v);
}

// Get luma variance for the pixel at xy.
// This function is either resolving variance from moments texture (first pass),
// or fetches it from 4th component of a color image (subsequent passes).
float SampleVariance(in uint2 xy, bool resolve)
{
    if (resolve)
    {
        float4 moments = g_moments[xy];
        return abs(moments.y - moments.x * moments.x);
    }
    else
    {
        return g_color[xy].w;
    }
}

// Resample variance using 3x3 Gaussian kernel as per SVGF paper.
float RasampleVariance(in uint2 xy, bool resolve)
{
    const float kVarianceSigma = 2.f;

    float filtered_variance = 0.f;
    float total_weight = 0.f;
    float center_variance = SampleVariance(xy, resolve);

    const int kRadius = 1;
    for (int dy = -kRadius; dy <= kRadius; ++dy)
    {
        for (int dx = -kRadius; dx <= kRadius; ++dx)
        {
            int2 sxy = int2(xy) + int2(dx, dy);

            if (any(sxy < 0) || any(sxy > int2(g_constants.width - 1, g_constants.height - 1)))
            {
                continue;
            }

            float variance = SampleVariance(sxy, resolve);
            float weight = Gaussian(center_variance, variance, kVarianceSigma);

            filtered_variance += weight * variance;
            total_weight += weight;
        }
    }

    return total_weight > 0.f ? (filtered_variance / total_weight) : 0.f;
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
    bool resolve_moments = g_constants.stride == 1;

    float3 filtered_color = 0.f;
    float filtered_variance = 0.f;
    float total_weight = 0.f;
    float total_variance_weight = 0.f;

    float4 center_g = g_gbuffer.Load(int3(gidx, 0));
    float3 center_n = OctDecode(center_g.xy);
    float  center_d = center_g.w;
    float3 center_color = g_color[gidx].xyz;
    float center_variance = RasampleVariance(gidx, resolve_moments);

    // Handle background.
    if (center_d < 1e-5f)
    {
        g_output_color[gidx] = float4(center_color, center_variance);
        return;
    }

    // EAW filter weights.
    const float kWeights[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };

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

            float3 c = g_color[xy].xyz;
            float4 g = g_gbuffer.Load(int3(xy, 0));
            float3 n = OctDecode(g.xy);
            float  d = g.w;

            // Skip background.
            if (d < 1e-5f)
            {
                continue;
            }

            // Calculate luma weight.
            float luma_weight = CalculateLumaWeight(luminance(center_color), luminance(c), center_variance);
            // Calculate EAW weight.
            float h_weight = kWeights[abs(dx)] * kWeights[abs(dy)];
            // Сalculate depth and normal weight
            float weight = CalculateNormalWeight(center_n, n) * CalculateDepthWeight(center_d / 100.f, d / 100.f) * CalculateNormalWeight(center_n, n);

            // Filter color and variance.
            filtered_color += weight * h_weight * luma_weight * c;
            filtered_variance += h_weight * h_weight * weight * weight * luma_weight * luma_weight * SampleVariance(xy, resolve_moments);

            total_weight += weight * h_weight * luma_weight;
            total_variance_weight += h_weight * h_weight * weight * weight * luma_weight * luma_weight;
        }
    }

    // Output filtered color and variance.
    center_color = (total_weight < EPS) ? center_color : (filtered_color / total_weight);
    center_variance =  (total_variance_weight < EPS) ? center_variance : (filtered_variance / total_variance_weight);
    g_output_color[gidx] = float4(center_color, center_variance);
}