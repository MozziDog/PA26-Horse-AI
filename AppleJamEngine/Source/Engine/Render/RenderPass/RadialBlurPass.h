#pragma once

#include "Render/RenderPass/RenderPassBase.h"

class FRadialBlurPass final : public FRenderPassBase
{
public:
	FRadialBlurPass();
	bool BeginPass(const FPassContext& Ctx) override;
	void EndPass(const FPassContext& Ctx) override;
};
