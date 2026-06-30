#include "ActionAfterImagePass.h"
#include "RenderPassRegistry.h"

#include "Render/Device/D3DDevice.h"
#include "Render/Command/DrawCommandList.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/RenderConstants.h"

REGISTER_RENDER_PASS(FActionAfterImagePass)

FActionAfterImagePass::FActionAfterImagePass()
{
	PassType = ERenderPass::ActionAfterImage;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::Opaque,
		ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FActionAfterImagePass::BeginPass(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	if (Frame.ActionAfterImages.empty() || !Frame.SceneColorCopyTexture || !Frame.ViewportRenderTexture ||
		!Frame.DepthTexture || !Frame.DepthCopyTexture || !Frame.StencilCopySRV)
	{
		return false;
	}

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	FStateCache& Cache = Ctx.Cache;

	DC->OMSetRenderTargets(0, nullptr, nullptr);
	DC->CopyResource(Frame.SceneColorCopyTexture, Frame.ViewportRenderTexture);
	DC->CopyResource(Frame.DepthCopyTexture, Frame.DepthTexture);
	DC->OMSetRenderTargets(1, &Cache.RTV, Cache.DSV);

	ID3D11ShaderResourceView* SceneColorSRV = Frame.SceneColorCopySRV;
	ID3D11ShaderResourceView* StencilSRV = Frame.StencilCopySRV;
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &SceneColorSRV);
	DC->PSSetShaderResources(ESystemTexSlot::Stencil, 1, &StencilSRV);

	Cache.bForceAll = true;
	return true;
}

void FActionAfterImagePass::EndPass(const FPassContext& Ctx)
{
	ID3D11ShaderResourceView* NullSRV = nullptr;
	Ctx.Device.GetDeviceContext()->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
	Ctx.Device.GetDeviceContext()->PSSetShaderResources(ESystemTexSlot::Stencil, 1, &NullSRV);
}
