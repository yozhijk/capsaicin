
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

#include "utils.h"
#include "camera.h"
#include "sampling.h"
#include "color_space.h"
#include "math_functions.h"
#include "aabb.h"

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

float3 SampleHistory(in float2 uv, in uint2 dim)
{
    return ResampleBicubic(g_color_history, uv, dim);
}

uint GetHistoryLength(in float2 uv, in uint2 dim)
{
    float2 xy = UVtoXY(uv, dim);
    uint2 uxy = uint2(floor(xy));
    return g_moments_history[uxy].w;
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

float4 SampleMomentsHistory(in float2 uv, in uint2 dim)
{
    return ResampleBicubic(g_moments_history, uv, dim).xyzz;
}

// Calculate AABB of a 5x5 pixel neighbourhood in YCoCg color space.
AABB CalculateNeighbourhoodColorAABB(in uint2 xy, in uint2 dim, in float scale)
{
    // Radius of the neighbourhood in pixels.
    const int kNeghbourhoodRadius = 2;

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
        int2 sxy = clamp(int2(xy) + int2(i, j), int2(0, 0), int2(dim.x-1, dim.y-1));
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

        // Calculate pixel luminance.
        float v = luminance(SampleColor(XYtoUV(float2(sxy), dim), dim));

        // Update moments.
        m += float2(v, v * v);
        num_samples += 1.f;
    }

    return (num_samples > 0.f) ? (m * rcp(num_samples)) : 0.f;
}

float CalculateClosestDepth(in RWTexture2D<float4> gbuffer, in float2 xy, in float2 dims)
{
    // Fetch GBuffer data.
    float4 gbuffer_data = gbuffer.Load(int3(xy, 0));

    // We need to look for closest depth.
    float closest_depth = gbuffer_data.w;

    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
        {
            int2 tap_xy = int2(xy) + int2(dx, dy);

            if (tap_xy.x >= dims.x || tap_xy.y >= dims.y ||
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

    // Copy the rest of the history.
    float2 this_frame_xy = gidx;

    float2 frame_buffer_size = float2(g_constants.width, g_constants.height);

#ifdef UPSCALE2X
    uint2 sp_offset = uint2((g_constants.frame_count % 4) / 2,
                            (g_constants.frame_count % 4) % 2);
    uint2 this_pixel_offset = gidx % 2;
    float2 input_buffer_size = float2(g_constants.width >> 1, g_constants.height >> 1);
#else
    float2 input_buffer_size = frame_buffer_size;
#endif


    // Calculate UV coordinates for this frame.
    float2 subsample_location = 0.5f;
    float2 this_frame_uv = (this_frame_xy + subsample_location) / frame_buffer_size;

    // Fetch GBuffer data.
    float4 gbuffer_data = g_gbuffer.Load(int3(this_frame_xy, 0));

    // Background.
    if (gbuffer_data.w < 1e-5f)
    {
        float3 color = SampleColor(this_frame_uv, input_buffer_size);
        float2 moments = float2(luminance(color), luminance(color) * luminance(color));
        g_output_color_history[int2(this_frame_xy)] = float4(color, 0.f);
        g_output_moments_history[int2(this_frame_xy)] = float4(moments, 0.f, 1.f);
        return;
    }

    // Reconstruct hit point using depth buffer from the current frame.
    float3 hit_position = ReconstructWorldPosition(this_frame_uv, gbuffer_data.w, frame_buffer_size);

    // Reconstruct previous frame UV.
    float2 prev_frame_uv = CalculateImagePlaneUV(g_prev_camera, hit_position);
    float speed = 0;//length(prev_frame_uv - this_frame_uv);


    // Check if the fragment is outside of previous view frustum or this is first frame.
    bool disocclusion = any(prev_frame_uv < 0.f) || any(prev_frame_uv > 1.f) || g_constants.frame_count == 0;

    if (disocclusion)
    {
        // In case of a disocclusion, do bilateral resampling for color.
        float3 color = SampleColor(this_frame_uv, input_buffer_size);
        float2 moments = float2(luminance(color), luminance(color) * luminance(color));
        g_output_color_history[int2(this_frame_xy)] = float4(color, 0.f);
        g_output_moments_history[int2(this_frame_xy)] = float4(moments, 0.f, 1.f);
        return;
    }
    else
    {
        // Calculate reprojected pixel coordinates and clamp to image extents.
        float2 prev_frame_xy = UVtoXY(prev_frame_uv, frame_buffer_size);
        float4 prev_gbuffer_data = g_prev_gbuffer.Load(int3(prev_frame_xy, 0));

        float current_closest_depth = length(hit_position - g_prev_camera.position);//CalculateClosestDepth(g_gbuffer, this_frame_xy, frame_buffer_size);
        float prev_closest_depth = CalculateClosestDepth(g_prev_gbuffer, prev_frame_xy, frame_buffer_size);

        if (abs(prev_closest_depth - current_closest_depth) / current_closest_depth > 0.05)
        {
            float3 color = SampleColor(this_frame_uv, input_buffer_size);
            float2 moments = float2(luminance(color), luminance(color) * luminance(color));
            g_output_color_history[int2(this_frame_xy)] = float4(color, 0.f);
            g_output_moments_history[int2(this_frame_xy)] = float4(moments, 0.f, 1.f);
            return;
        }

        float alpha = g_constants.alpha;
        // Fetch history and moments history.
        float3 history = SampleHistory(prev_frame_uv, frame_buffer_size);
        // Prepare current frame data.
        float3 color = SampleColor(this_frame_uv, input_buffer_size);

        uint history_length = GetHistoryLength(prev_frame_uv, frame_buffer_size);

        if (history_length < kMaxHistoryLength)
        {
            float t = 1.f / (history_length + 1);
            alpha = min(alpha, 1.f - t);
        }

#ifdef UPSCALE2X
        if (any(this_pixel_offset != sp_offset))
        {
            alpha = 1.f;
            history_length -= 1;
        }
#endif

        float   variance = 0.0;
        float4  moments_history = SampleMomentsHistory(prev_frame_uv, frame_buffer_size);
        float2  moments = lerp(float2(luminance(color), luminance(color) * luminance(color)), moments_history.xy, alpha);

        variance = abs(moments.y - moments.x * moments.x);

        // Perform history blend.
        g_output_moments_history[int2(this_frame_xy)] = float4(moments, 0.f, history_length + 1);
        g_output_color_history[int2(this_frame_xy)] = float4(lerp(color, history, alpha), variance);
    }
}

// float2 DilatedVelocityXY(in float2 xy)
// {
//     // Fetch GBuffer data.
//     float4 gbuffer_data = g_gbuffer.Load(int3(xy, 0));

//     // We need to look for closest depth.
//     float closest_depth = gbuffer_data.w;
//     int cx = 0;
//     int cy = 0;

//     for (int dx = -1; dx <= 1; ++dx)
//         for (int dy = -1; dy <= 1; ++dy)
//         {
//             int2 tap_xy = int2(xy) + int2(dx, dy);

//             if (tap_xy.x >= g_constants.width || tap_xy.y >= g_constants.height ||
//                 tap_xy.x < 0 || tap_xy.y < 0)
//                 continue;

//             float4 g = g_gbuffer.Load(int3(tap_xy, 0));

//             if (g.w != 0.f && g.w < closest_depth)
//             {
//                 closest_depth = g.w;
//                 cx = dx;
//                 cy = dy;
//             }
//         }

//     return xy + float2(cx, cy);
// }

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
    float3 hit_position = ReconstructWorldPosition(this_frame_uv, gbuffer_data.w, frame_buffer_size);
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
            //alpha = 0.98f - velocity_adjustment * 0.12f;
            color_aabb_scale = 0.75f;
            alpha = 0.6f;
        }
        else
        {
            alpha = 0.98f;
            color_aabb_scale = 5.f;
        }

        alpha = min(g_constants.alpha, alpha);

        // Fetch color and history.
        float3 history = RGB2YCoCg(SimpleTonemap(SampleHistory(prev_frame_uv, frame_buffer_size)));
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