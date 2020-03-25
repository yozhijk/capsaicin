typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct Constants
{
    float something;
};

struct RayPayload
{
    float3 color;
    uint padding;
};

ConstantBuffer<Constants> g_constants : register(b0);
RaytracingAccelerationStructure g_scene : register(t0);
RWTexture2D<float4> g_output : register(u0);

[shader("raygeneration")]
void TraceVisibility()
{
    float2 uv;
    uv.x = float(DispatchRaysIndex().x) / DispatchRaysDimensions().x;
    uv.y = float(DispatchRaysIndex().y) / DispatchRaysDimensions().y;

    float znear = 0.01f;
    float3 d = normalize(float3(lerp(-0.01f, 0.01f, uv.x), lerp(-0.01f, 0.01f, 1.f - uv.y), -znear));

    RayDesc ray;
    ray.Origin = float3(0, 1, 3);
    ray.Direction = d;
    ray.TMin = 0.f;
    ray.TMax = 1000.f;

    RayPayload payload;
    TraceRay(g_scene, RAY_FLAG_FORCE_OPAQUE , ~0, 0, 0, 0, ray, payload);
    g_output[DispatchRaysIndex().xy] = float4(payload.color, 1.f);
}

[shader("closesthit")]
void Hit(inout RayPayload payload, in MyAttributes attr)
{
    payload.color = attr.barycentrics.xyy;
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.color = float3(0.9f, 0.1f, 0.1f);
}