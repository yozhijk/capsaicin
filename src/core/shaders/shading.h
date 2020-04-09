#ifndef SHADING_H
#define SHADING_H

#include "sampling.h"

struct BrdfSample
{
    float3 direction;
    float3 brdf;
    float pdf;
    float distance;
};

float3 Lambert_Evaluate(float3 n, float3 o)
{
    return 1.f / PI;
}

float3 Lambert_Pdf(float3 n, float3 o)
{
    return max(0.f, dot(n, o)) / PI;
}

BrdfSample Lambert_Sample(float2 s, float3 n)
{
    BrdfSample ss;
    ss.direction = MapToHemisphere(s, n, 1.f);
    ss.brdf = Lambert_Evaluate(n, ss.direction);
    ss.pdf = Lambert_Pdf(n, ss.direction);
    ss.distance = 100000.f;
    return ss;
}



#endif