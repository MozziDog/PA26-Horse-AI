// Generated from C:/Projects/Jungle_Week14_Team4/KraftonEngine/Content/Material/Particle/ParticleBeamTrail.uasset
// Domain: ParticleMesh

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
    float4 n_5 = Tex_Diffuse.Sample(LinearWrapSampler, Input.UV0);
    float4 n_138 = Input.VertexColor;
    float3 n_145 = ((n_5).rgb * (n_138).rgb);
    float n_167 = ((n_5).a * (n_138).a);
    FMaterialResult Result;
    Result.Color = n_145;
    Result.Emissive = float3(0, 0, 0);
    Result.Opacity = n_167;
    Result.UVOffset = float2(0, 0);
    return Result;
}


struct VS_Input_MeshParticleInstance
{
    float4 transform0    : INSTANCE_TRANSFORM0;
    float4 transform1    : INSTANCE_TRANSFORM1;
    float4 transform2    : INSTANCE_TRANSFORM2;
    float4 transform3    : INSTANCE_TRANSFORM3;
    float4 color         : INSTANCE_COLOR;
    int    subImageIndex : INSTANCE_SUBIMAGE;
    float4 dynamicParam  : INSTANCE_DYNAMICPARAM;
};

struct PS_Input_MaterialMeshParticle
{
    float4 position       : SV_POSITION;
    float3 normal         : NORMAL;
    float2 texcoord       : TEXCOORD0;
    float4 color          : COLOR;
    float  subImageIndex  : TEXCOORD1;
    float4 dynamicParam   : TEXCOORD2;
    float3 worldPos       : TEXCOORD3;
};

PS_Input_MaterialMeshParticle VS(VS_Input_PNCT vert, VS_Input_MeshParticleInstance inst)
{
    float4x4 worldMatrix = float4x4(
        inst.transform0,
        inst.transform1,
        inst.transform2,
        inst.transform3
    );
    float4 worldPos = mul(float4(vert.position, 1.0f), worldMatrix);
    // 비균일 스케일에서 노말 왜곡 방지: 역전치 행렬 사용
    float3x3 M = (float3x3)worldMatrix;
    float3x3 invTransM = transpose(float3x3(
        cross(M[1], M[2]),
        cross(M[2], M[0]),
        cross(M[0], M[1])
    ));
    float3 worldNormal = mul(vert.normal, invTransM);

    PS_Input_MaterialMeshParticle output;
    output.position       = mul(worldPos, mul(View, Projection));
    output.normal         = normalize(worldNormal);
    output.texcoord       = vert.texcoord;
    output.color          = vert.color * inst.color;
    output.subImageIndex  = inst.subImageIndex;
    output.dynamicParam   = inst.dynamicParam;
    output.worldPos       = worldPos.xyz / worldPos.w;
    return output;
}

float4 PS(PS_Input_MaterialMeshParticle input) : SV_TARGET
{
    FMaterialPixelInput MaterialInput;
    MaterialInput.UV0           = input.texcoord;
    MaterialInput.UV1           = float2(0, 0);
    MaterialInput.UV2           = float2(0, 0);
    MaterialInput.ParticleColor = input.color;
    MaterialInput.VertexColor   = input.color;
    MaterialInput.Time          = Time;
    MaterialInput.SubImageIndex = input.subImageIndex;
    MaterialInput.DynamicParam  = input.dynamicParam;
    MaterialInput.WorldPosition = input.worldPos;
    MaterialInput.WorldNormal = SafeNormalize3(input.normal, float3(0, 0, 1));
    MaterialInput.CameraPosition = CameraWorldPos;
    MaterialInput.ViewDirection = SafeNormalize3(CameraWorldPos - input.worldPos, MaterialInput.WorldNormal);

    FMaterialResult Result = EvaluateMaterial(MaterialInput);
    float3 BaseColor = Result.Color;

    float4 FinalColor = float4(BaseColor + Result.Emissive, Result.Opacity);
    clip(FinalColor.a - 0.01f);
    return ApplyFogTransparent(FinalColor, input.worldPos, CameraWorldPos);
}
