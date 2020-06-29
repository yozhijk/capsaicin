// clang-format off
#ifndef CAMERA_H
#define CAMERA_H

#include "sampling.h"

float2 CalculateImagePlaneUV(in Camera camera, in float3 position)
{
    // Image plane has a normal vector forward and is passing through the point
    // position + forward * focal_length.

    // Ray plane intersection:
    // dot(n, x - p) = 0
    // dot(n, o + t * d - p) = 0
    // dot(n, o - p) + t * dot (n, d) = 0
    // t = dot (n, p - o) / dot (n, d)
    float3 d = normalize(position - camera.position);
    float3 o = camera.position;
    float3 n = normalize(camera.forward);
    float3 p = o + n * camera.focal_length;

    //if (abs(dot(n, d)) < 1e-5f) return float2(-1000.f, -1000.f);

    // Intersection distance to an image plane.
    float t = dot(n, p - o) / dot(n, d);

    // if (t <= 0.f) return float2(-1000.f, -1000.f);

    float3 ip = o + t * d;
    float3 ipd = (ip - p);

    float u = dot(camera.right, ipd) / (0.5f * camera.sensor_size.x);
    float v = dot(camera.up, ipd) / (0.5f * camera.sensor_size.y);
    return 0.5f * float2(u, v) + 0.5f;
}

RayDesc CreatePrimaryRay(in uint2 xy, in uint2 dim)
{
    // TODO: debug
    // float2 s = Sample2D_BlueNoise4x4Stable(g_blue_noise, xy, g_constants.frame_count);
    float2 s = Sample2D_Halton23(g_constants.frame_count);

    // Calculate [0..1] image plane sample
    float2 img_sample = (float2(xy) + s) / float2(dim);

    // Transform into [-0.5, 0.5]
    float2 h_sample = img_sample - float2(0.5f, 0.5f);
    // Transform into [-dim/2, dim/2]
    float2 c_sample = h_sample * g_camera.sensor_size;

    RayDesc my_ray;
    // Calculate direction to image plane
    my_ray.Direction =
        normalize(g_camera.focal_length * g_camera.forward + c_sample.x * g_camera.right + c_sample.y * g_camera.up);
    // Origin == camera position + nearz * d
    my_ray.Origin = g_camera.position;
    my_ray.TMin = 0.0f;
    my_ray.TMax = 1e6f;
    return my_ray;
}

#endif // CAMERA_H
// clang-format on