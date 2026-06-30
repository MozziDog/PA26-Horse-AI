#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

cbuffer ActionAfterImageCB : register(b2)
{
    float4 Params0; // xy: screen direction, z: intensity, w: radius in pixels
    float4 Params1; // xy: inverse viewport size, z: sample count, w: stencil value
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    const float2 dir = normalize(Params0.xy);
    const float intensity = saturate(Params0.z);
    const float radius = max(Params0.w, 0.0f);
    const float2 invViewport = Params1.xy;
    const int sampleCount = clamp((int)round(Params1.z), 1, 16);
    const uint stencilValue = (uint)round(Params1.w);

    float4 scene = SceneColorTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    if (intensity <= 0.0f || radius <= 0.0f || dot(dir, dir) <= 0.0001f)
    {
        return scene;
    }

    uint width;
    uint height;
    SceneColorTexture.GetDimensions(width, height);

    float4 trail = 0.0f;
    float totalWeight = 0.0f;

    [loop]
    for (int i = 1; i <= sampleCount; ++i)
    {
        float t = (float)i / (float)sampleCount;
        float2 sampleUV = input.uv + dir * radius * t * invViewport;
        if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f))
        {
            continue;
        }

        int2 coord = int2(sampleUV * float2(width, height));
        coord = clamp(coord, int2(0, 0), int2((int)width - 1, (int)height - 1));

        uint stencil = StencilTexture.Load(int3(coord, 0)).g;
        if (stencil != stencilValue)
        {
            continue;
        }

        float weight = (1.0f - t) * (1.0f - t);
        trail += SceneColorTexture.SampleLevel(LinearClampSampler, sampleUV, 0) * weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.0f)
    {
        return scene;
    }

    float4 trailColor = trail / totalWeight;
    float trailAlpha = saturate(intensity * min(totalWeight, 1.0f));
    return lerp(scene, trailColor, trailAlpha);
}
