#ifndef MATH_FUNCTIONS_H
#define MATH_FUNCTIONS_H

#define EPS 1e-8f

float Gaussian(in float3 x, in float3 m, in float sigma)
{
    float a = dot(x - m, x - m) / (sigma * sigma);
    return exp(-0.5 * a);
}

float Gaussian(in float2 x, in float2 m, in float sigma)
{
    float a = dot(x - m, x - m) / (sigma * sigma);
    return exp(-0.5 * a);
}

float Gaussian(in float x, in float m, in float sigma)
{
    float a = dot(x - m, x - m) / (sigma * sigma);
    return exp(-0.5 * a);
}

float luminance(in float3 rgb)
{
    return dot(rgb, float3(0.299f, 0.587f, 0.114f));
}

float max_component(float3 v)
{
    return max(max(v.x, v.y), v.z);
}

// Octahedron vector encoding / decoding for normals.
// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
float2 OctWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0 : -1.0);
}

float2 OctEncode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}

float3 OctDecode(float2 f)
{
    f = f * 2.0 - 1.0;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float  t = saturate(-n.z);
    n.xy += n.xy >= 0.0 ? -t : t;
    return normalize(n);
}

float cubic(in float x, in float b, in float c)
{
    float y  = 0.0;
    float x2 = x * x;
    float x3 = x * x * x;

    if (x < 1.0)
    {
        y = (12.0 - 9.0 * b - 6.0 * c) * x3 + (-18.0 + 12.0 * b + 6.0 * c) * x2 + (6.0 - 2.0 * b);
    }
    else if (x <= 2.0)
    {
        y = (-b - 6.0 * c) * x3 + (6.0 * b + 30.0 * c) * x2 + (-12.0 * b - 48.0 * c) * x +
            (8.0 * b + 24.0 * c);
    }

    return y / 6.0;
}

#endif
