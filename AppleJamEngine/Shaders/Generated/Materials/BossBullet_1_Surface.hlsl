// Generated from C:/Projects/Jungle_Week14_Team4/KraftonEngine/Content/Material/BossBullet_1.uasset
// Domain: Surface

#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

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
    float3 BaseColor;
    float3 Normal;
    float Roughness;
    float Metallic;
    float3 Emissive;
    float Opacity;
};

FMaterialResult EvaluateMaterial(FMaterialPixelInput Input)
{
    float3 n_36 = float3(0.586498f, 0.000000f, 0.000000f);
    float3 n_42 = Input.WorldNormal;
    float3 n_39 = Input.ViewDirection;
    float n_52 = 3.000000f;
    float n_29 = saturate(pow(1.0f - clamp(dot(SafeNormalize3((n_42), float3(0, 0, 1)), SafeNormalize3((n_39), float3(0, 0, 1))), 0.0f, 1.0f), n_52) * 1.000000f + 0.000000f);
    float3 n_1 = float3(1.000000f, 1.000000f, 1.000000f);
    float3 n_45 = (float3(n_29, n_29, n_29) * n_1);
    float n_3 = 1.000000f;
    FMaterialResult Result;
    Result.BaseColor = n_36;
    Result.Normal = float3(0, 0, 1);
    Result.Roughness = 0.5f;
    Result.Metallic = 0.0f;
    Result.Emissive = n_45;
    Result.Opacity = n_3;
    return Result;
}


struct MaterialSurfaceVSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

MaterialSurfaceVSOutput VS(VS_Input_PNCTT input)
{
    MaterialSurfaceVSOutput output;
    float4 worldPos = mul(float4(input.position, 1.0f), Model);
    output.worldPos = worldPos.xyz;
    output.position = mul(mul(worldPos, View), Projection);
    output.normal = normalize(mul(input.normal, (float3x3)NormalMatrix));
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}

MaterialSurfaceVSOutput VS_InstancedStaticMesh(VS_Input_InstancedPNCTT input)
{
    MaterialSurfaceVSOutput output;
    float4x4 InstanceModel = float4x4(
        input.instanceRow0,
        input.instanceRow1,
        input.instanceRow2,
        input.instanceRow3);
    float4x4 WorldModel = mul(InstanceModel, Model);

    float4 worldPos = mul(float4(input.position, 1.0f), WorldModel);
    output.worldPos = worldPos.xyz;
    output.position = mul(mul(worldPos, View), Projection);
    output.normal = normalize(mul(input.normal, (float3x3)WorldModel));
    output.color = input.color * input.instanceColor;
    output.texcoord = input.texcoord;
    return output;
}


float4 PS(MaterialSurfaceVSOutput input) : SV_TARGET
{

    FMaterialPixelInput MaterialInput;
    MaterialInput.UV0           = input.texcoord;
    MaterialInput.UV1           = float2(0, 0);
    MaterialInput.UV2           = float2(0, 0);
    MaterialInput.ParticleColor = float4(1, 1, 1, 1);
    MaterialInput.VertexColor   = input.color;
    MaterialInput.Time          = Time;
    MaterialInput.SubImageIndex = 0.0f;
    MaterialInput.DynamicParam  = float4(0, 0, 0, 0);
    MaterialInput.WorldPosition = input.worldPos;
    MaterialInput.WorldNormal = SafeNormalize3(input.normal, float3(0, 0, 1));
    MaterialInput.CameraPosition = CameraWorldPos;
    MaterialInput.ViewDirection = SafeNormalize3(CameraWorldPos - input.worldPos, MaterialInput.WorldNormal);

    FMaterialResult Result = EvaluateMaterial(MaterialInput);
    float3 N = normalize(input.normal);

    float3 finalRgb = Result.BaseColor + Result.Emissive;
    float OutOpacity = saturate(Result.Opacity);

    return float4(finalRgb, OutOpacity);
}
