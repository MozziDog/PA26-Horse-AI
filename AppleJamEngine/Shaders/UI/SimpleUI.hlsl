// 신규 계층형 UI 의 단색 쿼드 셰이더.
// 정점 포맷/상수버퍼 레이아웃은 RmlUi.hlsl 과 동일(POSITION float2, COLOR float4,
// TEXCOORD float2 / b0: ViewportSize, Translation, Transform)하게 맞춰 입력 레이아웃을
// 그대로 재사용한다. 다만 PS 는 텍스처를 샘플하지 않고 정점 색을 그대로 출력해
// 텍스처/샘플러 바인딩 없이 단색 사각형을 그린다(진단 B1/D3, 사이클 5).
struct VSInput
{
	float2 Position : POSITION;
	float4 Color    : COLOR;
	float2 TexCoord : TEXCOORD0;
	// 둥근 모서리용(화면 px). RoundRect=(중심기준 로컬좌표 xy, 반쪽크기 zw), Radius=모서리 반지름.
	// Radius<=0 이면 PS 가 SDF 분기를 건너뛰어 기존 직각 쿼드와 완전히 동일하게 동작한다.
	float4 RoundRect : TEXCOORD1;
	float  Radius    : TEXCOORD2;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
	float4 Color    : COLOR;
	float2 TexCoord : TEXCOORD0;
	float4 RoundRect : TEXCOORD1;
	float  Radius    : TEXCOORD2;
};

cbuffer SimpleUICB : register(b0)
{
	float2 ViewportSize;
	float2 Translation;
	column_major float4x4 Transform;
};

// [사이클 8] 텍스처 × 정점색. 텍스처 없는 단색 요소는 패스가 1×1 흰색 SRV 를 바인드해 곱셈 항등.
// 샘플러 s0 는 프레임 단위 시스템 샘플러(LinearClamp) — 패스가 별도 바인드하지 않음.
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);

VSOutput VS(VSInput Input)
{
	VSOutput Output;
	// 픽셀(좌상단 원점) → NDC. RmlUi.hlsl 과 동일한 Y-down 변환(진단 B2).
	float4 PixelPosition = float4(Input.Position + Translation, 0.0f, 1.0f);
	PixelPosition = mul(Transform, PixelPosition);
	float2 NdcPosition = float2(
		(PixelPosition.x / ViewportSize.x) * 2.0f - 1.0f,
		1.0f - (PixelPosition.y / ViewportSize.y) * 2.0f
	);
	Output.Position = float4(NdcPosition, 0.0f, 1.0f);
	Output.Color = Input.Color;
	Output.TexCoord = Input.TexCoord;
	Output.RoundRect = Input.RoundRect;
	Output.Radius = Input.Radius;
	return Output;
}

// 둥근 사각형 SDF. p=중심기준 좌표, b=반쪽크기, r=모서리 반지름(화면 px). 내부<0, 외부>0.
float SdRoundedBox(float2 p, float2 b, float r)
{
	float2 q = abs(p) - b + r;
	return min(max(q.x, q.y), 0.0f) + length(max(q, 0.0f)) - r;
}

float4 PS(VSOutput Input) : SV_Target
{
	float4 OutColor = tex.Sample(samp, Input.TexCoord) * Input.Color;

	// 모서리 둥글기가 지정된 쿼드만 SDF 로 외곽을 깎고 1px 안티에일리어싱. Radius<=0 면 기존 직각 유지.
	if (Input.Radius > 0.0f)
	{
		float Dist = SdRoundedBox(Input.RoundRect.xy, Input.RoundRect.zw, Input.Radius);
		float Aa = max(fwidth(Dist), 1e-4f);
		OutColor.a *= 1.0f - smoothstep(-Aa, Aa, Dist);
	}
	return OutColor;
}
