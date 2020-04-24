
#include "camera.h"
#include "sampling.h"
#include "color_space.h"
#include "math_functions.h"
#include "aabb.h"

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
RWTexture2D<float4> g_history : register(u2);
RWTexture2D<float4> g_prev_gbuffer : register(u3);
RWTexture2D<float4> g_output_history : register(u4);


float2 UVtoXY(in float2 uv)
{
    float2 xy = uv * float2(g_constants.width, g_constants.height);
    xy = min(xy, float2(g_constants.width - 1, g_constants.height - 1));
    return xy;
}

float3 SampleBilinear(in RWTexture2D<float4> texture, in float2 uv)
{
    float2 xy = UVtoXY(uv) - 0.5f;
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
    // Transform into [-0.5, 0.5]
    float2 h_sample = uv - float2(0.5f, 0.5f);
    // Transform into [-dim/2, dim/2]
    float2 c_sample = h_sample * g_camera.sensor_size;

    // Calculate direction to image plane
    float3 d = normalize(g_camera.focal_length * g_camera.forward + c_sample.x * g_camera.right + c_sample.y * g_camera.up);
    // Origin == camera position + nearz * d
    float3 o = g_camera.position;

    float t = g_gbuffer.Load(int3(UVtoXY(uv), 0)).w;
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

float luminance(in float3 rgb)
{
    return dot(rgb, float3(0.299f, 0.587f, 0.114f));
}

// Resampling function is using blue-noise sample positions from previous frame.
float3 ResampleBicubic(in RWTexture2D<float4> texture, in float2 uv)
{
    float3 filtered = float3(0.f, 0.f, 0.f);
    float2 center_xy = UVtoXY(uv);
    float tw = 0.f;

    // 3x3 bicubic filter.
    for (int i = -1; i <= 1; i++)
        for (int j = -1; j <= 1; j++)
        {
            float2 current_xy = center_xy + float2(i, j);

            bool offscreen = any(current_xy < 0.f) || any(current_xy >= float2(g_constants.width, g_constants.height));

            if (!offscreen)
            {
                float3 value = texture.Load(int3(current_xy, 0)).xyz;

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
    return SampleBilinear(g_history, uv);
}

float3 SampleColor(in float2 uv)
{
    return SampleBilinear(g_color, uv);
}

// Calculate AABB of a 5x5 pixel neighbourhood in YCoCg color space.
AABB CalculateNeighbourhoodColorAABB(in uint2 xy, in float scale)
{
    // Radius of the neighbourhood in pixels.
    const int kNeghbourhoodRadius = 2;

    // Central pixel color.
    float3 center_color =  RGB2YCoCg(SimpleTonemap(g_color[xy].xyz));

    // Mean and second color moment for accumulation.
    float3 m1 = 0.f;
    float3 m2 = 0.f;

    // Go over the neighbourhood.
    for (int i = -kNeghbourhoodRadius; i <= kNeghbourhoodRadius; i++)
        for (int j = -kNeghbourhoodRadius; j <= kNeghbourhoodRadius; j++)
    {
        // Pixel coordinates of a current sample.
        int2 sxy = clamp(int2(xy) + int2(i, j), int2(0, 0), int2(g_constants.width-1, g_constants.height-1));
        float3 v = RGB2YCoCg(SimpleTonemap(g_color[sxy].xyz));

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

// Temporal accumulation kernel for low-frequence GI accumulation.
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void Accumulate(in uint2 gidx: SV_DispatchThreadID,
                in uint2 lidx: SV_GroupThreadID,
                in uint2 bidx: SV_GroupID)
{
    if (gidx.x >= g_constants.width || gidx.y >= g_constants.height)
        return;

    float2 this_frame_xy = gidx;
    float2 frame_buffer_size = float2(g_constants.width, g_constants.height);

    // Calculate UV coordinates for this frame.
    float2 subsample_location = 0.5f;//Sample2D_BlueNoise4x4(g_blue_noise, this_frame_xy, g_constants.frame_count);
    float2 this_frame_uv = (this_frame_xy + subsample_location) / frame_buffer_size;

    // Reconstruct hit point using depth buffer from the current frame.
    float3 hit_position = ReconstructWorldPosition(this_frame_uv);

    // Reconstruct previous frame UV.
    float2 prev_frame_uv = CalculateImagePlaneUV(g_prev_camera, hit_position);
    // float speed = length(prev_frame_uv - this_frame_uv);

    // Fetch GBuffer data.
    float4 gbuffer_data = g_gbuffer.Load(int3(this_frame_xy, 0));

    // Check if the fragment is outside of previous view frustum or this is first frame.
    bool disocclusion = any(prev_frame_uv < 0.f) || any(prev_frame_uv > 1.f) || g_constants.frame_count == 0 ||
        gbuffer_data.w < 1e-5f;

    if (disocclusion)
    {
        g_output_history[int2(this_frame_xy)] = float4(SampleColor(this_frame_uv), 1.f);
    }
    else
    {
        // Calculate reprojected pixel coordinates and clamp to image extents.
        float2 prev_frame_xy = UVtoXY(prev_frame_uv);

        float4 prev_gbuffer_data = g_prev_gbuffer.Load(int3(prev_frame_xy, 0));

        // Perform velocity adjustment.
        float alpha = g_constants.alpha;

        if (abs(prev_gbuffer_data.w - gbuffer_data.w) / gbuffer_data.w > 0.05f)
        {
            alpha = 0.01f;
        }

        float3 history = SampleHistory(prev_frame_uv);
        float3 color = SampleColor(this_frame_uv);

        g_output_history[int2(this_frame_xy)] = float4(lerp(color, history, alpha), 1.f);
    }
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
    float2 subsample_location = 0.5f;//Sample2D_BlueNoise4x4(g_blue_noise, this_frame_xy, g_constants.frame_count);
    float2 this_frame_uv = (this_frame_xy + subsample_location) / frame_buffer_size;

    // Reconstruct hit point using depth buffer from the current frame.
    float3 hit_position = ReconstructWorldPosition(this_frame_uv);

    // Reconstruct previous frame UV.
    float2 prev_frame_uv = CalculateImagePlaneUV(g_prev_camera, hit_position);
    // float speed = length(prev_frame_uv - this_frame_uv);

    // Fetch GBuffer data.
    float4 gbuffer_data = g_gbuffer.Load(int3(this_frame_xy, 0));

    // Check if the fragment is outside of previous view frustum or this is first frame.
    bool disocclusion = any(prev_frame_uv < 0.f) || any(prev_frame_uv > 1.f) || g_constants.frame_count == 0 ||
        gbuffer_data.w < 1e-5f;

    if (disocclusion)
    {
        // Output bicubic filtering in case of a disocclusion.
        g_output_history[int2(this_frame_xy)] = float4(SampleColor(this_frame_uv), 1.f);
    }
    else
    {
        // Calculate reprojected pixel coordinates and clamp to image extents.
        float2 prev_frame_xy = UVtoXY(prev_frame_uv);

        // Perform velocity adjustment.
        float velocity_adjustment = 0; //g_constants.adjust_velocity != 0 ? (min(g_constants.alpha * 0.3, 0.1f / speed)) : 0.f;
        float alpha = g_constants.alpha - velocity_adjustment;

        // Fetch geometric data to assist rectification.
        // float4 prev_gbuffer_data = g_prev_gbuffer.Load(int3(prev_frame_xy, 0));
        // if (abs(prev_gbuffer_data.w - gbuffer_data.w) / gbuffer_data.w > 0.05f)
        // {
        //     alpha = 0.01f;
        // }

        // Fetch color and history.
        float3 history = RGB2YCoCg(SimpleTonemap(SampleHistory(prev_frame_uv)));
        float3 color = RGB2YCoCg(SimpleTonemap(SampleColor(this_frame_uv)));

        // Calculate neighbourhood color AABB.
        AABB color_aabb = CalculateNeighbourhoodColorAABB(gidx, 1.f);

        // Clip history to AABB.
        history = ClipToAABB(color_aabb, history);

        // Blend current sample to history.
        color = InvertSimpleTonemap(YCoCg2RGB(lerp(color, history, alpha)));

        // gidx
        g_output_history[int2(this_frame_xy)] = float4(color, 1.f);
    }
}