// clang-format off
#ifndef DATA_PAYLOAD_H
#define DATA_PAYLOAD_H

#define INVALID_ID ~0u

struct Camera
{
    float3  position;
    float   focal_length;
    float3  right;
    float   znear;
    float3  forward;
    float   focus_distance;
    float3  up;
    float   aperture;
    float2  sensor_size;
};

struct Mesh
{
    uint    vertex_count;
    uint    first_vertex_offset;
    uint    index_count;
    uint    first_index_offset;

    uint    index;
    uint    texture_index;
    uint2   padding;
};


#endif // DATA_PAYLOAD_H
// clang-format on