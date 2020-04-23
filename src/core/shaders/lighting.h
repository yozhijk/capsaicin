#ifndef LIGHTING_H
#define LIGHTING_H

struct LightSample
{
    float3 direction;
    float3 intensity;
    float pdf;
    float distance;
};

LightSample DirectionalLight_Sample(uint count)
{
    float t = 2.f * 3.14 * float(count % 4096) / 4096.f;
    float ly = 100.f;
    float lx = 40.f * sin(t);
    float lz = 40.f * cos(t);

    LightSample ls;
    ls.direction = normalize(float3(lx, ly, lz));
    ls.intensity = 3.f * float3(14.f, 12.f, 10.f) + float3(0.f, 0.f, 2.f + 2.f * cos(t));
    ls.pdf = 1.f;
    ls.distance = 100000.f;
    return ls;
}

#endif