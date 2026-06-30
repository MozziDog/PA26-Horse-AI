#include "ActionAfterImageMaskPass.h"
#include "RenderPassRegistry.h"

REGISTER_RENDER_PASS(FActionAfterImageMaskPass)

FActionAfterImageMaskPass::FActionAfterImageMaskPass()
{
	PassType = ERenderPass::ActionAfterImageMask;
	RenderState = { EDepthStencilState::ActionMaskWrite, EBlendState::NoColor,
		ERasterizerState::SolidBackCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}
