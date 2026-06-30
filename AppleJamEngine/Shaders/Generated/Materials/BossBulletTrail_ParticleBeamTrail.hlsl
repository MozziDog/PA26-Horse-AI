// Generated from C:/Projects/Jungle_Week14_Team4/KraftonEngine/Content/Material/BossBulletTrail.uasset
// Domain: ParticleBeamTrail

#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"
#define USE_FOG 1
#include "Common/Fog.hlsli"

cbuffer ForwardFogParams : register(b7)
{
    float4 FwdFogColor;
    float  FwdFogDensity;
    float  FwdFogHeightFalloff;
    float  FwdFogBaseHeight;
    float  FwdFogStartDistance;
    float  FwdFogCutoffDistance;
    float  FwdFogMaxOpacity;
    float2 _fwdFogPad;
};

float4 ApplyFogTransparent(float4 color, float3 worldPos, float3 cameraWorldPos)
{
    float fogFactor = ComputeHeightFogFactor(
        worldPos, cameraWorldPos,
        FwdFogDensity, FwdFogHeightFalloff, FwdFogBaseHeight,
        FwdFogStartDistance, FwdFogCutoffDistance, FwdFogMaxOpacity);
    color.rgb = lerp(color.rgb, FwdFogColor.rgb, fogFactor);
    return color;
}

float3 SafeNormalize3(float3 V, float3 Fallback)
{
    float LenSq = dot(V, V);
    return LenSq > 1e-8f ? V * rsqrt(LenSq) : Fallback;
}

struct FMaterialPixelInput
{
    float2 UV0;
    float2 UV1;
    float2 UV2;
    float4 ParticleColor;
    float4 VertexColor;
    float  Time;
    float  SubImageIndex;
    float4 DynamicParam;
    float3 WorldPosition;
    float3 WorldNormal;
    float3 CameraPosition;
    float3 ViewDirection;
};

struct FMaterialResult
{
    float3 Color;
    float3 Emissive;
    float Opacity;
    float2 UVOffset;
};

Texture2D Tex_Diffuse : register(t0);

FMaterialResult EvaluateMaterial(FMaterialPixelInput Input)
{
    float4 n_22 = Tex_Diffuse.Sample(LinearWrapSampler, Input.UV0);
    float4 n_37 = Input.VertexColor;
    float3 n_32 = ((n_22).rgb * (n_37).rgb);
    float n_46 = ((n_22).a * (n_37).a);
    FMaterialResult Result;
    Result.Color = n_32;
    Result.Emissive = float3(0, 0, 0);
    Result.Opacity = n_46;
    Result.UVOffset = float2(0, 0);
    return Result;
}


struct VS_Input_MaterialBeamTrail
{
    float3 position : POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXTCOORD;
};

struct PS_Input_MaterialBeamTrail
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR;
    float3 worldPos : TEXCOORD1;
};

PS_Input_MaterialBeamTrail VS(VS_Input_MaterialBeamTrail input)
{
    PS_Input_MaterialBeamTrail output;
    output.position = ApplyVP(input.position);
    output.texcoord = input.texcoord;
    output.color    = input.color;
    output.worldPos = input.position;
    return output;
}

float4 PS(PS_Input_MaterialBeamTrail input) : SV_TARGET
{
    FMaterialPixelInput MaterialInput;
    MaterialInput.UV0           = input.texcoord;
    MaterialInput.UV1           = float2(0, 0);
    MaterialInput.UV2           = float2(0, 0);
    MaterialInput.ParticleColor = input.color;
    MaterialInput.VertexColor   = input.color;
    MaterialInput.Time          = Time;
    MaterialInput.SubImageIndex = 0.0f;
    MaterialInput.DynamicParam  = float4(0, 0, 0, 0);
    MaterialInput.WorldPosition = input.worldPos;
    MaterialInput.WorldNormal = SafeNormalize3(CameraWorldPos - input.worldPos, float3(0, 0, 1));
    MaterialInput.CameraPosition = CameraWorldPos;
    MaterialInput.ViewDirection = MaterialInput.WorldNormal;

    FMaterialResult Result = EvaluateMaterial(MaterialInput);
    float4 FinalColor = float4(Result.Color + Result.Emissive, Result.Opacity);
    clip(FinalColor.a - 0.01f);
    return ApplyFogTransparent(FinalColor, input.worldPos, CameraWorldPos);
}
