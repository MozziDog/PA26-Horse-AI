#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

cbuffer CameraRadialBlurCB : register(b2)
{
    float4 Params0; // xy: center uv, z: intensity, w: protected center radius
    float4 Params1; // x: sample count
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    const float2 uv = input.uv;
    const float2 center = saturate(Params0.xy);
    const float intensity = saturate(Params0.z);
    const float protectedRadius = saturate(Params0.w);
    const int sampleCount = clamp((int)round(Params1.x), 1, 24);

    float4 scene = SceneColorTexture.SampleLevel(LinearClampSampler, uv, 0);
    if (intensity <= 0.001f)
    {
        return scene;
    }

    float2 toCenter = center - uv;
    float distanceToCenter = length(toCenter);
    if (distanceToCenter <= 0.0001f)
    {
        return scene;
    }

    float edgeMask = smoothstep(protectedRadius, 0.88f, distanceToCenter);
    if (edgeMask <= 0.001f)
    {
        return scene;
    }

    // Smear along the center ray. Sampling both inward and outward makes the
    // effect visible even on thin particles and silhouettes near the edge.
    float2 radialDir = toCenter / distanceToCenter;
    float maxPull = intensity * edgeMask * edgeMask * 0.34f;
    float4 accumulated = scene;
    float totalWeight = 1.0f;

    [loop]
    for (int i = 1; i <= sampleCount; ++i)
    {
        float t = (float)i / (float)sampleCount;
        float offset = maxPull * (t * t);
        float weight = 1.2f - t * 0.45f;

        float2 inwardUV = saturate(uv + radialDir * offset);
        float2 outwardUV = saturate(uv - radialDir * offset);
        accumulated += SceneColorTexture.SampleLevel(LinearClampSampler, inwardUV, 0) * weight;
        accumulated += SceneColorTexture.SampleLevel(LinearClampSampler, outwardUV, 0) * weight * 0.72f;
        totalWeight += weight * 1.72f;
    }

    float4 blurred = accumulated / totalWeight;
    float blendAmount = saturate(intensity * (0.45f + edgeMask * 0.85f) * edgeMask);
    return lerp(scene, blurred, blendAmount);
}
