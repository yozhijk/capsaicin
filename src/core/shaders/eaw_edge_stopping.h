#ifndef EDGE_STOPPING_H
#define EDGE_STOPPING_H

float CalculateNormalWeight(float3 n0, float3 n1, float s)
{
    return pow(max(dot(n0, n1), 0.0), s);
}

float CalculateDepthWeight(float dc, float dp, float s)
{
    float t = s == 0.f ? 0.0f : (abs(dc - dp) / s);
    return exp(-t);
}

float CalculateLumaWeight(float lc, float lp, float s)
{
    float v = abs(lc - lp) / s;
    return exp(-v);
}

#endif