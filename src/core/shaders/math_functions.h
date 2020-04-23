#ifndef MATH_FUNCTIONS_H
#define MATH_FUNCTIONS_H

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

float max_component(float3 v)
{
    return max(max(v.x, v.y), v.z);
}

#endif
