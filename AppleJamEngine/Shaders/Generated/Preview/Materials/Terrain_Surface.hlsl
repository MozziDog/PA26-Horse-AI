// Generated from D:/InternProject/HorseAI/AppleJamEngine/Content/Material/Terrain/Terrain.uasset
// Domain: Surface

#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Common/NormalMapping.hlsli"

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
    float3 Specular;
    float3 Emissive;
    float Opacity;
};

Texture2D Tex_Diffuse : register(t0);

FMaterialResult EvaluateMaterial(FMaterialPixelInput Input)
{
    float4 n_18 = Tex_Diffuse.Sample(LinearWrapSampler, Input.UV0);
    float n_75 = 0.300000f;
    float3 n_69 = ((n_18).rgb * float3(n_75, n_75, n_75));
    float4 n_32 = Tex_Diffuse.Sample(LinearWrapSampler, Input.UV0);
    float4 n_58 = Tex_Diffuse.Sample(LinearWrapSampler, Input.UV0);
    float4 n_45 = Tex_Diffuse.Sample(LinearWrapSampler, Input.UV0);
    float n_3 = 1.000000f;
    FMaterialResult Result;
    Result.BaseColor = n_69;
    Result.Normal = (n_32).rgb;
    Result.Roughness = ((n_58).rgb).x;
    Result.Metallic = ((n_45).rgb).x;
    Result.Specular = float3(1, 1, 1);
    Result.Emissive = float3(0, 0, 0);
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
    float4 tangent : TANGENT;
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
    float3 T = BuildOrthonormalTangent(output.normal, mul(input.tangent.xyz, (float3x3)Model));
    output.tangent = float4(T, input.tangent.w);
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
    float3 T = BuildOrthonormalTangent(output.normal, mul(input.tangent.xyz, (float3x3)WorldModel));
    output.tangent = float4(T, input.tangent.w);
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
    float3 N = SafeNormalize3(input.normal, float3(0, 0, 1));
    float3 materialNormal = Result.Normal;
    float3 tangentNormal = all(abs(materialNormal - float3(0, 0, 1)) < 1e-5f)
        ? float3(0, 0, 1)
        : materialNormal * 2.0f - 1.0f;
    N = ApplyTangentSpaceNormal(N, input.tangent.xyz, input.tangent.w, tangentNormal);

    float3 finalRgb = Result.BaseColor + Result.Emissive;
    float OutOpacity = saturate(Result.Opacity);

    return float4(finalRgb, 1.0f);
}
