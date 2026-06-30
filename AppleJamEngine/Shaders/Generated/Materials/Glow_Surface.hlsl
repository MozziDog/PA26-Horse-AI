// Generated from C:/Github/Week14/Jungle_Week14_Team4/KraftonEngine/Content/Material/Auto/Glow.uasset
// Domain: Surface

#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Common/ForwardLighting.hlsli"

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
};

struct FMaterialResult
{
    float3 BaseColor;
    float3 Normal;
    float Roughness;
    float Metallic;
    float3 Specular;
    float3 Emissive;
    float Opacity;
};

FMaterialResult EvaluateMaterial(FMaterialPixelInput Input)
{
    float3 n_1 = float3(0.800000f, 0.800000f, 0.800000f);
    float n_5 = 0.089087f;
    float3 n_7 = float3(0.500000f, 0.500000f, 0.500000f);
    float3 n_3 = float3(17.699999f, 17.699999f, 17.699999f);
    float n_9 = 1.000000f;
    FMaterialResult Result;
    Result.BaseColor = n_1;
    Result.Normal = float3(0, 0, 1);
    Result.Roughness = n_5;
    Result.Metallic = 0.0f;
    Result.Specular = n_7;
    Result.Emissive = n_3;
    Result.Opacity = n_9;
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

    FMaterialResult Result = EvaluateMaterial(MaterialInput);
    float3 N = normalize(input.normal);

    float3 V = normalize(CameraWorldPos - input.worldPos);
    float3 diffuse = AccumulateDiffuse(input.worldPos, N, input.position);
    float materialRoughness = clamp(Result.Roughness, 0.02f, 1.0f);
    float materialShininess = max(1.0f, (2.0f / (materialRoughness * materialRoughness)) - 2.0f);
    float3 specular = AccumulateSpecular(input.worldPos, N, V, materialShininess, input.position) * Result.Specular;

    float3 finalRgb = Result.BaseColor * diffuse + specular + Result.Emissive;
    float OutOpacity = saturate(Result.Opacity);

    return float4(finalRgb, OutOpacity);
}
