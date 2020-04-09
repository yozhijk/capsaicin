#ifndef MATH_FUNCTIONS_H
#define MATH_FUNCTIONS_H

float Gaussian(float3 x, float3 m, float sigma)
{
    float a = dot(x - m, x - m) / (sigma * sigma);
    return exp(-0.5 * a);
}

float Gaussian(float2 x, float2 m, float sigma)
{
    float a = dot(x - m, x - m) / (sigma * sigma);
    return exp(-0.5 * a);
}

float Gaussian(float x, float m, float sigma)
{
    float a = dot(x - m, x - m) / (sigma * sigma);
    return exp(-0.5 * a);
}

#endif
