typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct Constants
{
    uint width;
    uint height;
    uint frame_count;
    uint padding;
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
Texture2D<uint4> g_blue_noise: register(t1);
RWTexture2D<float4> g_output : register(u0);
RWBuffer<uint> g_index_buffer : register(u1);
RWBuffer<float> g_vertex_buffer : register(u2);
RWBuffer<float> g_normal_buffer : register(u3);
RWBuffer<float2> g_texcoord_buffer : register(u4);

float2 Sample2D_BlueNoise(in uint2 xy)
{
    float2 value =  float2(g_blue_noise.Load(int3(xy % 256, 0)).xy) / 255.f;
    return frac(value + 0.61803398875f * (g_constants.frame_count % 64));
}

RayDesc CreatePrimaryRay(in uint2 xy, in uint2 dim)
{
    float2 s = Sample2D_BlueNoise(xy);

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
    uint prim_index = PrimitiveIndex();
    uint instance_index = InstanceIndex();

    uint i0 = g_index_buffer[3 * prim_index + 0];
    uint i1 = g_index_buffer[3 * prim_index + 1];
    uint i2 = g_index_buffer[3 * prim_index + 2];

    float3 v0 = float3(g_vertex_buffer[3 * i0], g_vertex_buffer[3 * i0 + 1], g_vertex_buffer[3 * i0 + 2]);
    float3 v1 = float3(g_vertex_buffer[3 * i1], g_vertex_buffer[3 * i1 + 1], g_vertex_buffer[3 * i1 + 2]);
    float3 v2 = float3(g_vertex_buffer[3 * i2], g_vertex_buffer[3 * i2 + 1], g_vertex_buffer[3 * i2 + 2]);

    float2 t0 = g_texcoord_buffer[i0];
    float2 t1 = g_texcoord_buffer[i1];
    float2 t2 = g_texcoord_buffer[i2];

    float3 n0 = float3(g_normal_buffer[3 * i0], g_normal_buffer[3 * i0 + 1], g_normal_buffer[3 * i0 + 2]);
    float3 n1 = float3(g_normal_buffer[3 * i1], g_normal_buffer[3 * i1 + 1], g_normal_buffer[3 * i1 + 2]);
    float3 n2 = float3(g_normal_buffer[3 * i2], g_normal_buffer[3 * i2 + 1], g_normal_buffer[3 * i2 + 2]);

    float2 uv = attr.barycentrics.xy;
    float3 n = normalize(n0 * (1.f - uv.x - uv.y) + n1 * uv.x + n2 * uv.y);

    payload.color = 0.5f * n + 0.5f;// float3(v0);//0.5f * n + 0.5f;
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.color = float3(0.5f, 0.1f, 0.1f);
}