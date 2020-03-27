typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct Constants
{
    float something;
};

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

struct RayPayload
{
    float3 color;
    uint padding;
};

ConstantBuffer<Constants> g_constants : register(b0);
ConstantBuffer<Camera> g_camera : register(b1);
RaytracingAccelerationStructure g_scene : register(t0);
RWTexture2D<float4> g_output : register(u0);

RayDesc CreatePrimaryRay(in uint2 xy, in uint2 dim)
{
    float2 s = float2(0.5f, 0.5f);

    // Calculate [0..1] image plane sample
    float2 img_sample = (float2(xy) + s) / float2(dim);

    // Transform into [-0.5, 0.5]
    float2 h_sample = img_sample - float2(0.5f, 0.5f);
    // Transform into [-dim/2, dim/2]
    float2 c_sample = h_sample * g_camera.sensor_size;

    RayDesc my_ray;
    // Calculate direction to image plane
    my_ray.Direction = normalize(g_camera.focal_length * g_camera.forward + c_sample.x * g_camera.right + c_sample.y * g_camera.up);
    // Origin == camera position + nearz * d
    my_ray.Origin = g_camera.position;
    my_ray.TMin = 0.f;
    my_ray.TMax = 1e6f;
    return my_ray;
}

[shader("raygeneration")]
void TraceVisibility()
{
    RayDesc ray = CreatePrimaryRay(DispatchRaysIndex(), DispatchRaysDimensions());

    RayPayload payload;
    TraceRay(g_scene, RAY_FLAG_FORCE_OPAQUE , ~0, 0, 0, 0, ray, payload);

    uint2 output_xy = DispatchRaysIndex();
    output_xy.y = DispatchRaysDimensions().y - output_xy.y;

    g_output[output_xy] = float4(payload.color, 1.f);
}

[shader("closesthit")]
void Hit(inout RayPayload payload, in MyAttributes attr)
{
    payload.color = attr.barycentrics.xyy;
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.color = float3(0.5f, 0.1f, 0.1f);
}