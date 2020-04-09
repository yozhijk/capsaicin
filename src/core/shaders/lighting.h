#ifndef LIGHTING_H
#define LIGHTING_H

struct LightSample
{
    float3 direction;
    float3 intensity;
    float pdf;
    float distance;
};

LightSample DirectionalLight_Sample()
{
    LightSample ls;
    ls.direction = normalize(float3(0.3f, 1.f, 0.3f));
    ls.intensity = float3(12.f, 10.f, 10.f);
    ls.pdf = 1.f;
    ls.distance = 100000.f;
    return ls;
}

#endif