// clang-format off
#ifndef UTILS_H
#define UTILS_H

// Convert UV space coordinates to image coordinates.
float2 UVtoXY(in float2 uv, in uint2 dim)
{
    float2 xy = uv * float2(dim);
    return min(xy, float2(dim.x - 1, dim.y - 1));
}

// Convert image space coordinates to UV coordinates.
float2 XYtoUV(in float2 xy, in uint2 dim)
{
    return clamp(xy / float2(dim), 0.f, 1.f);
}


// Sample texture with bilinear filtering.
float3 SampleBilinear(in RWTexture2D<float4> texture,
                      in float2 uv,
                      in uint2 dim)
{
    float2 xy = UVtoXY(uv, dim) - 0.5f;
    uint2 uxy = uint2(floor(xy));

    float2 w    = frac(xy);
    float3 v00  = texture[uxy].xyz;
    float3 v01  = texture[uxy + uint2(0, 1)];
    float3 v10  = texture[uxy + uint2(1, 0)];
    float3 v11  = texture[uxy + uint2(1, 1)];

    return lerp(lerp(v00, v10, w.x),
                lerp(v01, v11, w.x), w.y);
}


#endif // UTILS_H
// clang-format on