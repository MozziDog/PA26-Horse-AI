#include "SimpleUIPass.h"

#include "RenderPassRegistry.h"
#include "Render/Types/FrameContext.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Command/DrawCommandList.h"
#include "Core/Types/CoreTypes.h"

#include "UI/Canvas/UICanvasManager.h"
#include "UI/Canvas/UICanvas.h"
#include "UI/Canvas/UIElement.h"
#include "UI/Canvas/UIImage.h"
#include "Object/Object.h"

#include <d3d11.h>

REGISTER_RENDER_PASS(FSimpleUIPass)

namespace
{
	// 정점 레이아웃은 SimpleUI.hlsl 의 VSInput 선언 순서와 정확히 일치해야 한다(입력 레이아웃이
	// APPEND_ALIGNED 리플렉션으로 생성되므로 순서=오프셋). 둥근 모서리용 RoundRect/Radius 는 끝에 추가.
	struct FSimpleUIVertex
	{
		float X, Y;             // POSITION
		float R, G, B, A;       // COLOR
		float U, V;             // TEXCOORD0
		float LocalX, LocalY;   // TEXCOORD1.xy — 중심기준 로컬좌표(화면 px)
		float HalfW, HalfH;     // TEXCOORD1.zw — 반쪽크기(화면 px)
		float Radius;           // TEXCOORD2    — 모서리 반지름(화면 px, 0=직각)
	};

	struct FSimpleUICB
	{
		float ViewportWidth = 1.0f;
		float ViewportHeight = 1.0f;
		float TranslationX = 0.0f;
		float TranslationY = 0.0f;
		float Transform[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
		};
	};

	constexpr const char* SimpleUIShaderPath = "Shaders/UI/SimpleUI.hlsl";

	// [사이클 8] 같은 SRV(텍스처) 연속 쿼드의 인덱스 런. 트리 순서대로 쌓아 텍스처 경계에서만 분리.
	struct FUIBatch { ID3D11ShaderResourceView* SRV; UINT IndexCount; };

	// 가시 노드의 ScreenRect 를 쿼드(4정점 / 6인덱스)로 누적 + 요소별 SRV 로 배칭. top-down 트리 순회.
	// GlobalScale: 레퍼런스 px 인 CornerRadius 를 화면 px 로 환산하는 배율.
	void CollectVisible(UUIElement* Element, ID3D11Device* Device, ID3D11ShaderResourceView* WhiteSRV,
	                    float GlobalScale, TArray<FSimpleUIVertex>& Verts, TArray<uint32>& Indices,
	                    TArray<FUIBatch>& Batches)
	{
		if (!Element)
		{
			return;
		}

		// [show/off] 숨긴 요소는 자신과 하위 트리 전체를 렌더에서 제외(조기 반환으로 자식 재귀도 중단).
		if (!Element->IsVisible())
		{
			return;
		}

		if (Element->IsVisibleRect())
		{
			// [사이클 8] UUIImage 의 텍스처 SRV, 없으면(또는 비-이미지면) 1×1 흰색 fallback.
			ID3D11ShaderResourceView* SRV = WhiteSRV;
			if (UUIImage* Img = Cast<UUIImage>(Element))
			{
				if (ID3D11ShaderResourceView* Tex = Img->ResolveTextureSRV(Device))
				{
					SRV = Tex;
				}
			}

			const FUIRect& R = Element->GetScreenRect();
			const FVector4 C = Element->GetColor();
			const uint32 Base = static_cast<uint32>(Verts.size());

			const float X0 = R.Pos.X;
			const float Y0 = R.Pos.Y;
			const float X1 = R.Pos.X + R.Size.X;
			const float Y1 = R.Pos.Y + R.Size.Y;

			// 둥근 모서리 SDF 파라미터(화면 px). 반지름은 레퍼런스 px → 화면 px 환산 후, 변의 절반을
			// 넘지 않게 클램프(초과 시 SDF 가 음수 r 로 깨짐). 0 이면 PS 가 SDF 를 건너뜀.
			const float HalfW = (X1 - X0) * 0.5f;
			const float HalfH = (Y1 - Y0) * 0.5f;
			float Radius = Element->GetEffectiveCornerRadius() * GlobalScale;
			const float MaxRadius = HalfW < HalfH ? HalfW : HalfH;
			if (Radius > MaxRadius) Radius = MaxRadius;
			if (Radius < 0.0f) Radius = 0.0f;

			// 각 꼭짓점의 중심기준 로컬좌표 = (±HalfW, ±HalfH). PS 가 보간해 프래그먼트 오프셋을 얻는다.
			Verts.push_back({ X0, Y0, C.R, C.G, C.B, C.A, 0.0f, 0.0f, -HalfW, -HalfH, HalfW, HalfH, Radius });
			Verts.push_back({ X1, Y0, C.R, C.G, C.B, C.A, 1.0f, 0.0f,  HalfW, -HalfH, HalfW, HalfH, Radius });
			Verts.push_back({ X1, Y1, C.R, C.G, C.B, C.A, 1.0f, 1.0f,  HalfW,  HalfH, HalfW, HalfH, Radius });
			Verts.push_back({ X0, Y1, C.R, C.G, C.B, C.A, 0.0f, 1.0f, -HalfW,  HalfH, HalfW, HalfH, Radius });

			Indices.push_back(Base + 0);
			Indices.push_back(Base + 1);
			Indices.push_back(Base + 2);
			Indices.push_back(Base + 0);
			Indices.push_back(Base + 2);
			Indices.push_back(Base + 3);

			// [A-1] 직전 런과 같은 SRV 면 확장, 다르면 새 draw call(텍스처 경계). z-순서(트리 순서) 보존.
			if (!Batches.empty() && Batches.back().SRV == SRV)
			{
				Batches.back().IndexCount += 6;
			}
			else
			{
				Batches.push_back({ SRV, 6 });
			}
		}

		for (USceneComponent* Child : Element->GetChildren())
		{
			if (UUIElement* ChildElement = Cast<UUIElement>(Child))
			{
				CollectVisible(ChildElement, Device, WhiteSRV, GlobalScale, Verts, Indices, Batches);
			}
		}
	}

	ID3D11Buffer* CreateBuffer(ID3D11Device* Device, UINT BindFlags, const void* Data, UINT ByteWidth)
	{
		if (!Device || ByteWidth == 0)
		{
			return nullptr;
		}
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.ByteWidth = ByteWidth;
		Desc.BindFlags = BindFlags;

		D3D11_SUBRESOURCE_DATA Init = {};
		Init.pSysMem = Data;

		ID3D11Buffer* Buffer = nullptr;
		if (FAILED(Device->CreateBuffer(&Desc, Data ? &Init : nullptr, &Buffer)))
		{
			return nullptr;
		}
		return Buffer;
	}
}

FSimpleUIPass::FSimpleUIPass()
{
	PassType = ERenderPass::SimpleUI;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::AlphaBlend,
	                ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

FSimpleUIPass::~FSimpleUIPass()
{
	if (WhiteSRV)
	{
		WhiteSRV->Release();
		WhiteSRV = nullptr;
	}
}

// [사이클 8] 텍스처 없는 단색 요소도 텍스처 셰이더를 통과하도록 1×1 흰색 SRV 를 1회 생성·캐시.
ID3D11ShaderResourceView* FSimpleUIPass::GetWhiteSRV(ID3D11Device* Device)
{
	if (WhiteSRV || !Device)
	{
		return WhiteSRV;
	}
	const uint32 WhitePixel = 0xffffffff;
	D3D11_TEXTURE2D_DESC TexDesc = {};
	TexDesc.Width = 1;
	TexDesc.Height = 1;
	TexDesc.MipLevels = 1;
	TexDesc.ArraySize = 1;
	TexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TexDesc.SampleDesc.Count = 1;
	TexDesc.Usage = D3D11_USAGE_DEFAULT;
	TexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA Init = {};
	Init.pSysMem = &WhitePixel;
	Init.SysMemPitch = sizeof(uint32);

	ID3D11Texture2D* Tex = nullptr;
	if (SUCCEEDED(Device->CreateTexture2D(&TexDesc, &Init, &Tex)))
	{
		Device->CreateShaderResourceView(Tex, nullptr, &WhiteSRV);
		Tex->Release();
	}
	return WhiteSRV;
}

bool FSimpleUIPass::BeginPass(const FPassContext& Ctx)
{
	// 그릴 Canvas 가 하나도 없거나 뷰포트 RTV 가 없으면 패스 스킵.
	return Ctx.Frame.ViewportRTV && !FUICanvasManager::Get().GetCanvases().empty();
}

void FSimpleUIPass::Execute(const FPassContext& Ctx)
{
	ID3D11Device* Device = Ctx.Device.GetDevice();
	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	if (!Device || !DC || Ctx.Frame.ViewportWidth <= 0.0f || Ctx.Frame.ViewportHeight <= 0.0f)
	{
		return;
	}

	// 1) 레이아웃 패스가 캐시한 ScreenRect 들을 쿼드로 모은다(텍스처별 런으로 배칭). 레이아웃 없음.
	TArray<FSimpleUIVertex> Verts;
	TArray<uint32> Indices;
	TArray<FUIBatch> Batches;
	ID3D11ShaderResourceView* White = GetWhiteSRV(Device);
	const float GlobalScale = FUICanvasManager::Get().GetGlobalScale();
	for (UUICanvas* Canvas : FUICanvasManager::Get().GetCanvases())
	{
		CollectVisible(Canvas, Device, White, GlobalScale, Verts, Indices, Batches);
	}
	if (Indices.empty())
	{
		return;
	}

	// 2) 셰이더 — 단색 쿼드(텍스처/샘플러 불필요).
	FShader* Shader = FShaderManager::Get().GetOrCreate(SimpleUIShaderPath);
	if (!Shader || !Shader->IsValid())
	{
		return;
	}

	// 3) 프레임 단위 임시 버퍼 생성(소량 쿼드 — MVP 단순화). 그린 뒤 해제.
	ID3D11Buffer* VB = CreateBuffer(Device, D3D11_BIND_VERTEX_BUFFER,
		Verts.data(), static_cast<UINT>(sizeof(FSimpleUIVertex) * Verts.size()));
	ID3D11Buffer* IB = CreateBuffer(Device, D3D11_BIND_INDEX_BUFFER,
		Indices.data(), static_cast<UINT>(sizeof(uint32) * Indices.size()));
	ID3D11Buffer* CB = CreateBuffer(Device, D3D11_BIND_CONSTANT_BUFFER, nullptr, sizeof(FSimpleUICB));
	if (!VB || !IB || !CB)
	{
		if (VB) VB->Release();
		if (IB) IB->Release();
		if (CB) CB->Release();
		return;
	}

	// 4) 상태 — RmlUi 와 동일(NoDepth / AlphaBlend / SolidNoCull), 뷰포트 RTV 에 합성.
	Ctx.Resources.SetDepthStencilState(Ctx.Device, EDepthStencilState::NoDepth);
	Ctx.Resources.SetBlendState(Ctx.Device, EBlendState::AlphaBlend);
	Ctx.Resources.SetRasterizerState(Ctx.Device, ERasterizerState::SolidNoCull);

	D3D11_VIEWPORT Viewport = {};
	Viewport.TopLeftX = 0.0f;
	Viewport.TopLeftY = 0.0f;
	Viewport.Width = Ctx.Frame.ViewportWidth;
	Viewport.Height = Ctx.Frame.ViewportHeight;
	Viewport.MinDepth = 0.0f;
	Viewport.MaxDepth = 1.0f;
	DC->RSSetViewports(1, &Viewport);

	DC->OMSetRenderTargets(1, &Ctx.Cache.RTV, Ctx.Cache.DSV);
	DC->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Shader->Bind(DC);

	FSimpleUICB CBData;
	CBData.ViewportWidth = Ctx.Frame.ViewportWidth;
	CBData.ViewportHeight = Ctx.Frame.ViewportHeight;
	DC->UpdateSubresource(CB, 0, nullptr, &CBData, 0, 0);
	DC->VSSetConstantBuffers(0, 1, &CB);

	UINT Stride = sizeof(FSimpleUIVertex);
	UINT Offset = 0;
	DC->IASetVertexBuffers(0, 1, &VB, &Stride, &Offset);
	DC->IASetIndexBuffer(IB, DXGI_FORMAT_R32_UINT, 0);

	// [사이클 8, A-1] 텍스처 런마다 SRV(t0) 바인드 후 DrawIndexed. 트리 순서대로 재생 → z-순서 보존.
	// 샘플러 s0 는 BeginFrame 이 프레임 단위로 바인드(시스템 LinearClamp) — 여기서 설정 불필요.
	UINT StartIndex = 0;
	for (const FUIBatch& Batch : Batches)
	{
		DC->PSSetShaderResources(0, 1, &Batch.SRV);
		DC->DrawIndexed(Batch.IndexCount, StartIndex, 0);
		StartIndex += Batch.IndexCount;
	}

	VB->Release();
	IB->Release();
	CB->Release();
}
