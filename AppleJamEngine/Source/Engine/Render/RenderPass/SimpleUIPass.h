#pragma once

#include "Render/RenderPass/RenderPassBase.h"

struct ID3D11Device;
struct ID3D11ShaderResourceView;

// 신규 계층형 UI 의 draw-only 렌더패스(진단 B4, 사이클 5).
// FUICanvasManager 가 보유한 Canvas 트리에서 사이클 3 레이아웃 패스가 캐시해 둔
// 화면 사각형(ScreenRect)만 읽어 쿼드로 제출한다. 여기서 레이아웃은 하지 않는다.
// enum 순서상 RmlUi(ERenderPass::UI) 바로 앞이라 RmlUi 텍스트가 SimpleUI 쿼드 위에 그려진다.
// [사이클 8] 요소가 텍스처(UUIImage)를 가지면 그 SRV 를, 아니면 1×1 흰색 SRV 를 바인드(텍스처 × 정점색).
class FSimpleUIPass final : public FRenderPassBase
{
public:
	FSimpleUIPass();
	~FSimpleUIPass() override;

	bool BeginPass(const FPassContext& Ctx) override;
	void Execute(const FPassContext& Ctx) override;

private:
	// 1×1 흰색 SRV(텍스처 없는 단색 요소용 — 같은 셰이더로 텍스처×색의 곱셈 항등). 최초 1회 생성·캐시.
	ID3D11ShaderResourceView* GetWhiteSRV(ID3D11Device* Device);
	ID3D11ShaderResourceView* WhiteSRV = nullptr;
};
