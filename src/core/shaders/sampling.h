#ifndef SAMPLING_H
#define SAMPLING_H

#define PI 3.141592653589793238463f

float2 Sample2D_BlueNoise(in Texture2D<uint4> texture, in uint2 xy, in uint count)
{
    // 256 is blue-noise texture size.
    float2 value =  float2(texture.Load(int3(xy % 256, 0)).xy) / 255.f;
    return frac(value + 0.61803398875f * count);
}

float2 Sample2D_BlueNoise4x4(in Texture2D<uint4> texture, in uint2 xy, in uint count)
{
    uint px = (count % 16) % 4;
    uint py = (count % 16) / 4;

    uint2 sxy = xy * 4 + uint2(px, py);

    // 256 is blue-noise texture size.
    float2 value =  float2(texture.Load(int3(sxy % 256, 0)).xy) / 255.f;
    return frac(value + 0.61803398875f * (count / 16));
}

uint WangHash(in uint2 xy)
{
    const uint M = 1664525u, C = 1013904223u;
    uint seed = (xy.x * M + xy.y + C) * M;
    seed ^= (seed >> 11u);
    seed ^= (seed << 7u) & 0x9d2c5680u;
    seed ^= (seed << 15u) & 0xefc60000u;
    seed ^= (seed >> 18u);
    return seed;
}

float2 Sample2D_BlueNoise4x4Stable(in Texture2D<uint4> texture, in uint2 xy, in uint count)
{
    uint px = (count % 16) % 4;
    uint py = (count % 16) / 4;

    uint2 sxy = uint2(px, py);

    // 256 is blue-noise texture size.
    float2 value =  float2(texture.Load(int3(sxy % 256, 0)).xy) / 255.f;
    return value;
}

float Sample1D_Hammersley(in uint bits, in uint seed)
{
    bits = ( bits << 16u) | ( bits >> 16u);
    bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
    bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
    bits ^= seed;
    return float(bits) * 2.3283064365386963e-10; // divide by 1<<32
}

float2 Sample2D_Hammersley(in uint2 xy, in uint frame_count, in uint cell_size)
{
    uint seed1 = WangHash(xy);
    uint seed2 = WangHash(uint2(seed1, 1000));

    uint index = frame_count % cell_size;

    float u = float(seed1) * 2.3283064365386963e-10;
    float uu = frac(u + index * rcp(cell_size));
    float vv = frac(Sample1D_Hammersley(index, seed2));

    return float2(uu, vv);
}

float2 Sample2D_Hammersley16(in uint2 xy, in uint frame_count)
{

    return Sample2D_Hammersley(xy, frame_count, 16);
}

// Create a vector orthogonal to a passed one.
float3 OrthoVector(in float3 n)
{
    float3 p;
	
    if (abs(n.z) > 0.0)
    {
        float k = length(n.yz);
        p.x = 0; p.y = -n.z / k; p.z = n.y / k;
    }
    else
    {
        float k = length(n.xy);
        p.x = n.y / k; p.y = -n.x / k; p.z = 0;
    }
	
    return p;
}

float3 MapToHemisphere(in float2 s, in float3 n, in float e)
{
    // Construct basis
    float3 u = OrthoVector(n);
    float3 v = cross(u, n);
    u = cross(n, v);
	
    // Calculate 2D sample
    float r1 = s.x;
    float r2 = s.y;
	
    // Transform to spherical coordinates
    float sin_psi = sin(2 * PI * r1);
    float cos_psi = cos(2 * PI * r1);
    float cos_theta = pow(1.0 - r2, 1.0 / (e + 1.0));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
	
    // Return the result
    return normalize(u * sin_theta * cos_psi + v * sin_theta * sin_psi + n * cos_theta);
}

#endif // SAMPLING_H