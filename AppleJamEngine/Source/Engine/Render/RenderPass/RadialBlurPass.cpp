#include "RadialBlurPass.h"
#include "RenderPassRegistry.h"

#include "Render/Device/D3DDevice.h"
#include "Render/Command/DrawCommandList.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/RenderConstants.h"
#include "Core/Logging/Log.h"

REGISTER_RENDER_PASS(FRadialBlurPass)

FRadialBlurPass::FRadialBlurPass()
{
	PassType = ERenderPass::RadialBlur;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::Opaque,
		ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FRadialBlurPass::BeginPass(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	static bool bWasEnabled = false;
	if (Frame.CameraRadialBlur.bEnabled != bWasEnabled)
	{
		bWasEnabled = Frame.CameraRadialBlur.bEnabled;
		UE_LOG("[RadialBlurPass] %s intensity=%.3f radius=%.3f samples=%d center=(%.3f, %.3f)",
			bWasEnabled ? "enabled" : "disabled",
			Frame.CameraRadialBlur.Intensity,
			Frame.CameraRadialBlur.Radius,
			Frame.CameraRadialBlur.SampleCount,
			Frame.CameraRadialBlur.Center.X,
			Frame.CameraRadialBlur.Center.Y);
	}

	if (!Frame.CameraRadialBlur.bEnabled || Frame.CameraRadialBlur.Intensity <= 0.0f ||
		!Frame.SceneColorCopyTexture || !Frame.ViewportRenderTexture)
	{
		return false;
	}

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	FStateCache& Cache = Ctx.Cache;

	DC->CopyResource(Frame.SceneColorCopyTexture, Frame.ViewportRenderTexture);
	DC->OMSetRenderTargets(1, &Cache.RTV, Cache.DSV);

	ID3D11ShaderResourceView* SceneColorSRV = Frame.SceneColorCopySRV;
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &SceneColorSRV);

	Cache.bForceAll = true;
	return true;
}

void FRadialBlurPass::EndPass(const FPassContext& Ctx)
{
	ID3D11ShaderResourceView* NullSRV = nullptr;
	Ctx.Device.GetDeviceContext()->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
}
