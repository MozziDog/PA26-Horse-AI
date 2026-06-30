// Generated from C:/Projects/Jungle_Week14_Team4/KraftonEngine/Content/Material/Particle/ParticleBeamTrail.uasset
// Domain: ParticleSprite

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


struct VS_Input_ParticleQuad
{
    float3 cornerSign : POSITION;
    float2 baseUV     : TEXTCOORD;
};

struct VS_Input_ParticleInstance
{
    float3 position      : INSTANCE_CENTER;
    float3 velocity      : INSTANCE_VELOCITY;
    float2 size          : INSTANCE_SIZE;
    float  rotation      : INSTANCE_ROTATION;
    float4 color         : INSTANCE_COLOR;
    int    subImageIndex : INSTANCE_SUBIMAGE;
    int    alignment     : INSTANCE_ALIGNMENT;
    float4 dynamicParam  : INSTANCE_DYNAMICPARAM;
};

struct PS_Input_MaterialParticle
{
    float4 position       : SV_POSITION;
    float2 texcoord       : TEXCOORD0;
    float4 color          : COLOR;
    float  subImageIndex  : TEXCOORD1;
    float4 dynamicParam   : TEXCOORD2;
    float3 worldPos       : TEXCOORD3;
};

PS_Input_MaterialParticle VS(VS_Input_ParticleQuad quad, VS_Input_ParticleInstance inst)
{
    float sinR = sin(inst.rotation);
    float cosR = cos(inst.rotation);

    float2 corner = quad.cornerSign.xy;
    float2 rotUV = float2(
        corner.x * cosR - corner.y * sinR,
        corner.x * sinR + corner.y * cosR
    );

    float3 cameraRight = float3(View._m00, View._m10, View._m20);
    float3 cameraUp    = float3(View._m01, View._m11, View._m21);
    float3 worldPos = inst.position
                    + cameraRight * (rotUV.x * inst.size.x)
                    + cameraUp    * (rotUV.y * inst.size.y);

    PS_Input_MaterialParticle output;
    output.position       = mul(float4(worldPos, 1.0f), mul(View, Projection));
    output.texcoord       = quad.baseUV;
    output.color          = inst.color;
    output.subImageIndex  = inst.subImageIndex;
    output.dynamicParam   = inst.dynamicParam;
    output.worldPos       = worldPos;
    return output;
}

float4 PS(PS_Input_MaterialParticle input) : SV_TARGET
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
    MaterialInput.CameraPosition = CameraWorldPos;
    MaterialInput.ViewDirection = SafeNormalize3(CameraWorldPos - input.worldPos, float3(0, 0, 1));
    MaterialInput.WorldNormal = MaterialInput.ViewDirection;

    FMaterialResult Result = EvaluateMaterial(MaterialInput);
    float4 FinalColor = float4(Result.Color + Result.Emissive, Result.Opacity);
    clip(FinalColor.a - 0.01f);
    return ApplyFogTransparent(FinalColor, input.worldPos, CameraWorldPos);
}
