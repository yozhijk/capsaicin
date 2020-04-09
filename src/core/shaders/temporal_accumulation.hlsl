
#include "camera.h"
#include "sampling.h"

#define TILE_SIZE 8

struct Constants
{
    uint width;
    uint height;
    uint frame_count;
    uint padding;
};

ConstantBuffer<Constants> g_constants : register(b0);
ConstantBuffer<Camera> g_camera : register(b1);
ConstantBuffer<Camera> g_prev_camera : register(b2);
Texture2D<uint4> g_blue_noise: register(t0);
RWTexture2D<float4> g_color_direct : register(u0);
RWTexture2D<float4> g_color_indirect : register(u1);
RWTexture2D<float4> g_gbuffer : register(u2);
RWTexture2D<float4> g_history : register(u3);
RWTexture2D<float4> g_prev_gbuffer : register(u4);
RWTexture2D<float4> g_output_history : register(u5);

// This reconstruction is using the same blue-noise sample used by primary visibility pass.
float3 ReconstructWorldPosition(in uint2 xy)
{
    float2 s = Sample2D_BlueNoise4x4Stable(g_blue_noise, xy, g_constants.frame_count);

    // Calculate [0..1] image plane sample
    uint2 dim = uint2(g_constants.width, g_constants.height);
    float2 img_sample = (float2(xy) + s) / float2(dim);

    // Transform into [-0.5, 0.5]
    float2 h_sample = img_sample - float2(0.5f, 0.5f);
    // Transform into [-dim/2, dim/2]
    float2 c_sample = h_sample * g_camera.sensor_size;

    // Calculate direction to image plane
    float3 d = normalize(g_camera.focal_length * g_camera.forward + c_sample.x * g_camera.right + c_sample.y * g_camera.up);
    // Origin == camera position + nearz * d
    float3 o = g_camera.position;

    float t = g_gbuffer.Load(int3(xy, 0)).w;

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
float3 ResampleBicubic(in RWTexture2D<float4> texture, in uint2 uxy)
{
    float3 filtered = float3(0.f, 0.f, 0.f);
    int2 center_xy = uxy;
    float2 f_center_pos = float2(center_xy) + Sample2D_BlueNoise4x4Stable(g_blue_noise, center_xy, g_constants.frame_count - 1);
    float tw = 0.f;

    // 3x3 bicubic filter.
    for (int i = -1; i <= 1; i++)
        for (int j = -1; j <= 1; j++)
        {
            int2 current_xy = center_xy + int2(i, j);

            bool offscreen = any(current_xy < 0) || any(current_xy >= int2(g_constants.width, g_constants.height));

            if (!offscreen)
            {
                float3 value = texture.Load(int3(current_xy, 0)).xyz;

                // Use blue-noise sample position from previous frame.
                float2 s = Sample2D_BlueNoise4x4Stable(g_blue_noise, current_xy, g_constants.frame_count - 1);
                float2 f_current_pos = float2(current_xy) + s;


                float2 d = abs(f_current_pos - f_center_pos);
                float w = cubic(d.x, 0, 0.5) * cubic(d.y, 0, 0.5) * rcp(1.f + luminance(value));


                filtered += w * value;
                tw += w;
            }
        }

    return filtered / tw;
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void Accumulate(in uint2 gidx: SV_DispatchThreadID,
                in uint2 lidx: SV_GroupThreadID,
                in uint2 bidx: SV_GroupID)
{
    if (gidx.x >= g_constants.width || gidx.y >= g_constants.height)
        return;

    // Reconstruct hit point using depth buffer from the current frame.
    float3 hit_position = ReconstructWorldPosition(gidx);

    // Reconstruct normalized image plane coordinates in the previous frame (-1..1 range).
    float2 prev_frame_uv = CalculateImagePlaneCoordinates(g_prev_camera, hit_position);
    float2 this_frame_uv = CalculateImagePlaneCoordinates(g_camera, hit_position);
    float speed = length(prev_frame_uv - this_frame_uv);

    // Fetch GBuffer data.
    float4 gbuffer_data = g_gbuffer.Load(int3(gidx, 0));

    // Check if the fragment is outside of previous view frustum or this is first frame.
    bool disocclusion = any(prev_frame_uv < -1.f) || any(prev_frame_uv > 1.f) || g_constants.frame_count == 0 ||
        gbuffer_data.w < 1e-5f;

    if (disocclusion)
    {
        g_output_history[gidx] = float4(ResampleBicubic(g_color_indirect, gidx), 1.f);
    }
    else
    {
        float alpha = 0.98f - min(0.8, speed / 0.1f);
        uint2 reprojected_xy = uint2((0.5f * prev_frame_uv + 0.5f) * float2(g_constants.width, g_constants.height));
        reprojected_xy.x = min(reprojected_xy.x, g_constants.width - 1);
        reprojected_xy.y = min(reprojected_xy.y, g_constants.height - 1);

        float3 history = ResampleBicubic(g_history, reprojected_xy);
        float3 color = ResampleBicubic(g_color_indirect, gidx);

        g_output_history[gidx] = float4(lerp(color, history, alpha), 1.f);
    }
}