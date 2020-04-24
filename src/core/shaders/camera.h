#ifndef CAMERA_H
#define CAMERA_H

struct Camera
{
    float3 position;
    float focal_length;

    float3 right;
    float znear;

    float3 forward;
    float focus_distance;

    float3 up;
    float aperture;

    float2 sensor_size;
};

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

    if (abs(dot(n, d)) < 1e-5f) return float2(-1000.f, -1000.f);

    // Intersection distance to an image plane.
    float t = dot(n, p - o) / dot(n, d);

    if (t <= 0.f) return float2(-1000.f, -1000.f);

    float3 ip = o + t * d;
    float3 ipd = (ip - p);

    float u = dot(camera.right, ipd) / (0.5f * camera.sensor_size.x);
    float v = dot(camera.up, ipd) / (0.5f * camera.sensor_size.y);
    return 0.5f * float2(u, v) + 0.5f;
}

#endif // CAMERA_H