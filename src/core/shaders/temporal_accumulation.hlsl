
#include "data_payload.h"

#define TILE_SIZE 8

struct Constants
{
    uint width;
    uint height;
    uint frame_count;
    uint padding;

    float alpha;
    uint adjust_velocity;
    uint padding1;
    uint padding2;
};

ConstantBuffer<Constants> g_constants : register(b0);
ConstantBuffer<Camera> g_camera : register(b1);
ConstantBuffer<Camera> g_prev_camera : register(b2);
Texture2D<float4> g_blue_noise: register(t0);
RWTexture2D<float4> g_color : register(u0);
RWTexture2D<float4> g_gbuffer : register(u1);
RWTexture2D<float4> g_color_history : register(u2);
RWTexture2D<float4> g_moments_history : register(u3);
RWTexture2D<float4> g_prev_gbuffer : register(u4);
RWTexture2D<float4> g_output_color_history : register(u5);
RWTexture2D<float4> g_output_moments_history : register(u6);

#include "camera.h"
#include "sampling.h"
#include "color_space.h"
#include "math_functions.h"
#include "aabb.h"

float2 UVtoXY(in float2 uv, in uint2 dim)
{
    float2 xy = uv * float2(dim);
    xy = min(xy, float2(dim.x - 1, dim.y - 1));
    return xy;
}

float2 XYtoUV(in float2 xy, in uint2 dim)
{
    return clamp(xy / float2(dim), 0.f, 1.f);
}

float3 SampleBilinear(in RWTexture2D<float4> texture, in float2 uv, in uint2 dim)
{
    float2 xy = UVtoXY(uv, dim) - 0.5f;
    uint2 uxy = uint2(floor(xy));
    float2 w = frac(xy);

    float3 v00 = texture[uxy].xyz;
    float3 v01 = texture[uxy + uint2(0, 1)];
    float3 v10 = texture[uxy + uint2(1, 0)];
    float3 v11 = texture[uxy + uint2(1, 1)];

    return lerp(lerp(v00, v10, w.x), lerp(v01, v11, w.x), w.y);
}

float3 ReconstructWorldPosition(in float2 uv)
{
    uint2 frame_buffer_size = uint2(g_constants.width, g_constants.height);

    // Transform into [-0.5, 0.5]
    float2 h_sample = uv - float2(0.5f, 0.5f);
    // Transform into [-dim/2, dim/2]
    float2 c_sample = h_sample * g_camera.sensor_size;

    // Calculate direction to image plane
    float3 d = normalize(g_camera.focal_length * g_camera.forward + c_sample.x * g_camera.right + c_sample.y * g_camera.up);
    // Origin == camera position + nearz * d
    float3 o = g_camera.position;

    float t = g_gbuffer.Load(int3(UVtoXY(uv, frame_buffer_size), 0)).w;

    return o + t * d;
}

float cubic(in float x, in float b, in float c)
{
    float y = 0.0;
    float x2 = x * x;
    float x3 = x * x * x;

    if (x < 1.0)
    {
        y = (12.0 - 9.0 * b - 6.0 * c) * x3 + (-18.0 + 12.0 * b + 6.0 * c) * x2 + (6.0 - 2.0 * b);
    }
    else if (x <= 2.0)
    {
        y = (-b - 6.0 * c) * x3 + (6.0 * b + 30.0 * c) * x2 + (-12.0 * b - 48.0 * c) * x + (8.0 * b + 24.0 * c);
    }

    return y / 6.0;
}

// Resampling function is using blue-noise sample positions from previous frame.
float3 ResampleBicubic(in RWTexture2D<float4> texture, in float2 uv, in uint2 dim)
{
    float3 filtered = float3(0.f, 0.f, 0.f);
    float2 center_xy = UVtoXY(uv, dim);
    float tw = 0.f;

    // 3x3 bicubic filter.
    for (int i = -1; i <= 1; i++)
        for (int j = -1; j <= 1; j++)
        {
            float2 current_xy = center_xy + float2(i, j);

            bool offscreen = any(current_xy < 0.f) || any(current_xy >= dim);

            if (!offscreen)
            {
                float3 value =  SampleBilinear(texture, XYtoUV(current_xy, dim), dim);

                float2 d = abs(current_xy - center_xy);
                float w = cubic(d.x, 0, 0.5) * cubic(d.y, 0, 0.5) * rcp(1.f + luminance(value));

                filtered += w * value;
                tw += w;
            }
        }

    return tw > 1e-5f ? (filtered / tw) : 0.f;
}

float3 SampleHistory(in float2 uv)
{
    return ResampleBicubic(g_color_history, uv, uint2(g_constants.width, g_constants.height));
}

uint GetHistoryLength(in float2 uv)
{
    float2 xy = UVtoXY(uv, uint2(g_constants.width, g_constants.height));
    uint2 uxy = uint2(floor(xy));
    return g_color_history[uxy].w;
}

float3 SampleColor(in float2 uv, in uint2 dim)
{
    return SampleBilinear(g_color, uv, dim);
}

float3 SampleColorPoint(in float2 uv, in uint2 dim)
{
    float2 xy = UVtoXY(uv, dim);
    uint2 uxy = uint2(floor(xy));
    return g_color[uxy].xyz;
}

float4 SampleMomentsHistory(in float2 uv)
{
    return ResampleBicubic(g_moments_history, uv, uint2(g_constants.width, g_constants.height)).xyzz;
}

// Calculate AABB of a 5x5 pixel neighbourhood in YCoCg color space.
AABB CalculateNeighbourhoodColorAABB(in uint2 xy, in uint2 dim, in float scale)
{
    // Radius of the neighbourhood in pixels.
    const int kNeghbourhoodRadius = 1;

    // Central pixel color.
    float3 center_color =  RGB2YCoCg(SimpleTonemap(SampleColor(XYtoUV(xy, dim), dim)));

    // Mean and second color moment for accumulation.
    float3 m1 = 0.f;
    float3 m2 = 0.f;

    // Go over the neighbourhood.
    for (int i = -kNeghbourhoodRadius; i <= kNeghbourhoodRadius; i++)
        for (int j = -kNeghbourhoodRadius; j <= kNeghbourhoodRadius; j++)
    {
        // Pixel coordinates of a current sample.
        int2 sxy = clamp(int2(xy) + int2(i, j), int2(0, 0), int2(g_constants.width-1, g_constants.height-1));
        float3 v = RGB2YCoCg(SimpleTonemap(SampleColor(XYtoUV(sxy, dim), dim)));

        // Update moments.
        m1 += v;
        m2 += v * v;
    }

    // Both statistical moments require 1 / N division.
    const int kNumSamples = (2 * kNeghbourhoodRadius + 1) * (2 * kNeghbourhoodRadius + 1);
    m1 *= rcp(kNumSamples);
    m2 *= rcp(kNumSamples);

    // Calculate standard deviation.
    float3 dev = sqrt(abs(m2 - m1 * m1)) * scale;

    // Create color bounding box around mean.
    AABB aabb;
    aabb.pmin = min(m1 - dev, center_color);
    aabb.pmax = max(m1 + dev, center_color);

    return aabb;
}

// Calculate first and second luminance moments in 7x7 region centered at uv.
float2 CalculateLumaMomentsSpatial(in float2 uv, in uint2 dim)
{
    int2 xy = UVtoXY(uv, dim);

    // Gbuffer data at kernel center.
    float4 gc = g_gbuffer.Load(int3(xy, 0));

    // Mean and second color moment for accumulation.
    float2 m = 0.f;
    float num_samples = 0.f;

    // Go over the neighbourhood.
    const int kNeghbourhoodRadius = 3;
    for (int i = -kNeghbourhoodRadius; i <= kNeghbourhoodRadius; i++)
        for (int j = -kNeghbourhoodRadius; j <= kNeghbourhoodRadius; j++)
    {
        // Pixel coordinates of a current sample.
        int2 sxy = int2(xy) + int2(i, j);

        // Check out of bounds.
        if (any(sxy < 0) || any(sxy > int2(dim.x - 1, dim.y - 1)))
        {
            continue;
        }

        // GBuffer data at current pixel.
        float4 g = g_gbuffer.Load(int3(sxy, 0));

        // TODO: reiterate on GBuffer culling later.
        // if (g.z != gc.z || g.w < 1e-5f ||
        //     abs(g.w - gc.w) / gc.w > 0.05f)
        // {
        //     continue;
        // }

        // Calculate pixel luminance.
        float v = luminance(SampleColorPoint(XYtoUV(float2(sxy), dim), dim));

        // Update moments.
        m += float2(v, v * v);
        num_samples += 1.f;
    }

    return (num_samples > 0.f) ? (m * rcp(num_samples)) : 0.f;
}

float CalculateClosestDepth(in RWTexture2D<float4> gbuffer, in float2 xy)
{
    // Fetch GBuffer data.
    float4 gbuffer_data = gbuffer.Load(int3(xy, 0));

    // We need to look for closest depth.
    float closest_depth = gbuffer_data.w;

    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
        {
            int2 tap_xy = int2(xy) + int2(dx, dy);

            if (tap_xy.x >= g_constants.width || tap_xy.y >= g_constants.height ||
                tap_xy.x < 0 || tap_xy.y < 0)
                continue;

            float4 g = gbuffer.Load(int3(tap_xy, 0));

            if (g.w != 0.f && g.w < closest_depth)
            {
                closest_depth = g.w;
            }
        }

    return closest_depth;
}

// Temporal accumulation kernel with upscaling for low-frequency GI accumulation.
// The kernel accumulates irradiance as well as 1st and 2nd luminance moments
// for the successive SVGF denoiser as per:
// "Spatiotemporal Variance-Guided Filtering: Real-Time Reconstruction for Path-Traced Global Illumination" Schied et Al
// https://research.nvidia.com/sites/default/files/pubs/2017-07_Spatiotemporal-Variance-Guided-Filtering%3A//svgf_preprint.pdf
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void Accumulate(in uint2 gidx: SV_DispatchThreadID,
                in uint2 lidx: SV_GroupThreadID,
                in uint2 bidx: SV_GroupID)
{
    const uint kMaxHistoryLength = 256.f;

    if (gidx.x >= g_constants.width || gidx.y >= g_constants.height)
        return;

    float2 this_frame_xy = gidx;
    float2 frame_buffer_size = float2(g_constants.width, g_constants.height);
    //float2 input_buffer_size = float2(g_constants.width >> 1, g_constants.height >> 1);
    float2 input_buffer_size = float2(g_constants.width, g_constants.height);

    // Calculate UV coordinates for this frame.
    float2 subsample_location = 0.5f;
    float2 this_frame_uv = (this_frame_xy + subsample_location) / frame_buffer_size;

    // Reconstruct hit point using depth buffer from the current frame.
    float3 hit_position = ReconstructWorldPosition(this_frame_uv);

    // Reconstruct previous frame UV.
    float2 prev_frame_uv = CalculateImagePlaneUV(g_prev_camera, hit_position);
    float speed = 0;//length(prev_frame_uv - this_frame_uv);

    // Fetch GBuffer data.
    float4 gbuffer_data = g_gbuffer.Load(int3(this_frame_xy, 0));

    // Check if the fragment is outside of previous view frustum or this is first frame.
    bool disocclusion = any(prev_frame_uv < 0.f) || any(prev_frame_uv > 1.f) || g_constants.frame_count == 0 ||
         gbuffer_data.w < 1e-5f;

    if (disocclusion)
    {
        // In case of a disocclusion, do bilateral resampling for color
        // and estimate spatial luma moments, instead of temporal.
        float3 color = SampleColor(this_frame_uv, input_buffer_size);
        float2 luma_moments = CalculateLumaMomentsSpatial(this_frame_uv, input_buffer_size);

        g_output_color_history[int2(this_frame_xy)] = float4(color, 1.f);
        g_output_moments_history[int2(this_frame_xy)] = float4(luma_moments, 0.f, 1.f);
    }
    else
    {
        // Calculate reprojected pixel coordinates and clamp to image extents.
        float2 prev_frame_xy = UVtoXY(prev_frame_uv, frame_buffer_size);
        float4 prev_gbuffer_data = g_prev_gbuffer.Load(int3(prev_frame_xy, 0));

        // Perform velocity adjustment.
        //float alpha = g_constants.alpha;
        float velocity_adjustment = 0.f;//max(0.f, 1.f - 0.1f / speed);
        float alpha = max(0.1f, g_constants.alpha - velocity_adjustment);

        // Preserve history if this pixel does not have active samples evaluated in this frame.
        // bool skip_pixel = !Interleave2x2(this_frame_xy, g_constants.frame_count);

        // TODO: this feels as a bad heuristic, reiterate later.
        float current_closest_depth = CalculateClosestDepth(g_gbuffer, this_frame_xy);
        float prev_closest_depth = CalculateClosestDepth(g_prev_gbuffer, prev_frame_xy);

        if (abs(prev_closest_depth - current_closest_depth) / current_closest_depth > 0.1)
        {
            // In case of a disocclusion, do bilateral resampling for color
            // and estimate spatial luma moments, instead of temporal.
            float3 color = SampleColor(this_frame_uv, input_buffer_size);
            float2 luma_moments = CalculateLumaMomentsSpatial(this_frame_uv, input_buffer_size);

            g_output_color_history[int2(this_frame_xy)] = float4(color, 1.f);
            g_output_moments_history[int2(this_frame_xy)] = float4(luma_moments, 0.f, 1.f);
            return;
        }

        // Fetch history and moments history.
        float3 history = SampleHistory(prev_frame_uv);
        float4 moments_history = SampleMomentsHistory(prev_frame_uv);
        float2 luma_moments_spatial = CalculateLumaMomentsSpatial(this_frame_uv, input_buffer_size);
        uint history_length = GetHistoryLength(prev_frame_uv);


        // Prepare current frame data.
        float3 color = SampleColor(this_frame_uv, input_buffer_size);
        float luma = luminance(color);


        // float2 m = lerp(luma_moments_spatial, float2(luma, luma * luma), min(1, history_length / 16));
        float4 moment = float4(luma, luma * luma, 0.f, 1.f);

        if (history_length < kMaxHistoryLength)
        {
            float t = 1.f / (history_length + 1);
            alpha = min(alpha, 1.f - t);
        }

        // if (skip_pixel) alpha = 1.f;

        // Perform history blend.
        g_output_color_history[int2(this_frame_xy)] = float4(lerp(color, history, alpha), history_length + 1);
        g_output_moments_history[int2(this_frame_xy)] = lerp(moment, moments_history, alpha);
    }
}

float2 DilatedVelocityXY(in float2 xy)
{
    // Fetch GBuffer data.
    float4 gbuffer_data = g_gbuffer.Load(int3(xy, 0));

    // We need to look for closest depth.
    float closest_depth = gbuffer_data.w;
    int cx = 0;
    int cy = 0;

    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
        {
            int2 tap_xy = int2(xy) + int2(dx, dy);

            if (tap_xy.x >= g_constants.width || tap_xy.y >= g_constants.height ||
                tap_xy.x < 0 || tap_xy.y < 0)
                continue;

            float4 g = g_gbuffer.Load(int3(tap_xy, 0));

            if (g.w != 0.f && g.w < closest_depth)
            {
                closest_depth = g.w;
                cx = dx;
                cy = dy;
            }
        }

    return xy + float2(cx, cy);
}

// Traditional TAA kernel with some optimizations described in 
// "A Survey of Temporal Antialiasing Techniques" Eurographics 2020, Lei Yang, Shiqiu Liu, Marco Salvi
// http://behindthepixels.io/assets/files/TemporalAA.pdf
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void TAA(in uint2 gidx: SV_DispatchThreadID,
         in uint2 lidx: SV_GroupThreadID,
         in uint2 bidx: SV_GroupID)
{
    if (gidx.x >= g_constants.width || gidx.y >= g_constants.height)
        return;

    // Frame buffer dimensions.
    float2 this_frame_xy = gidx;
    float2 frame_buffer_size = float2(g_constants.width, g_constants.height);
    // Calculate UV coordinates for this frame.
    float2 subsample_location = 0.5f;
    float2 this_frame_uv = (this_frame_xy + subsample_location) / frame_buffer_size;

    float4 gbuffer_data = g_gbuffer.Load(int3(this_frame_xy, 0));

    // Background.
    if (gbuffer_data.w < 1e-5f)
    {
        // Output bicubic filtering in case of a disocclusion.
        g_output_color_history[int2(this_frame_xy)] = float4(SampleColor(this_frame_uv, frame_buffer_size), 1.f);
        return;
    }

    // Reconstruct hit point using depth buffer from the current frame.
    float3 hit_position = ReconstructWorldPosition(this_frame_uv);

    // Reconstruct previous frame UV.
    float2 prev_frame_uv = CalculateImagePlaneUV(g_prev_camera, hit_position);
    // Velocity in pixels.
    float velocity = length((prev_frame_uv - this_frame_uv) * frame_buffer_size);

    // Check if the fragment is outside of previous view frustum or this is first frame.
    bool disocclusion = any(prev_frame_uv < 0.f) || any(prev_frame_uv > 1.f);

    if (disocclusion)
    {
        // Output bicubic filtering in case of a disocclusion.
        g_output_color_history[int2(this_frame_xy)] = float4(SampleColor(this_frame_uv, frame_buffer_size), 1.f);
    }
    else
    {
        // Calculate reprojected pixel coordinates and clamp to image extents.
        float2 prev_frame_xy = UVtoXY(prev_frame_uv, frame_buffer_size);

        // Perform velocity adjustment.
        // float velocity_adjustment = g_constants.adjust_velocity != 0 ? (min(g_constants.alpha * 0.3, 0.1f / speed)) : 0.f;
        // float alpha = g_constants.alpha - velocity_adjustment;

        bool is_static = velocity < 1e-3f;

        float alpha = g_constants.alpha;
        float color_aabb_scale = 1.f;

        if (!is_static)
        {
            float velocity_adjustment = max(velocity / 8.f, 1.f);
            alpha = 0.98f - velocity_adjustment * 0.15f;
            color_aabb_scale = 5.f - velocity_adjustment * 4.f;
        }
        else
        {
            alpha = 0.98f;
            color_aabb_scale = 5.f;
        }

        alpha = min(g_constants.alpha, alpha);

        // Fetch color and history.
        float3 history = RGB2YCoCg(SimpleTonemap(SampleHistory(prev_frame_uv)));
        float3 color = RGB2YCoCg(SimpleTonemap(SampleColor(this_frame_uv, frame_buffer_size)));

        // Calculate neighbourhood color AABB.
        AABB color_aabb = CalculateNeighbourhoodColorAABB(gidx, frame_buffer_size, color_aabb_scale);

        // Clip history to AABB.
        history = ClipToAABB(color_aabb, history);

        // Blend current sample to history.
        color = InvertSimpleTonemap(YCoCg2RGB(lerp(color, history, alpha)));

        // gidx
        g_output_color_history[int2(this_frame_xy)] = float4(color, 1.f);
    }
}