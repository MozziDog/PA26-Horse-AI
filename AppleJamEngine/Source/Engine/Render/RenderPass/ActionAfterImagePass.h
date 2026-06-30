#pragma once

#include "RenderPassBase.h"

class FActionAfterImagePass final : public FRenderPassBase
{
public:
	FActionAfterImagePass();
	bool BeginPass(const FPassContext& Ctx) override;
	void EndPass(const FPassContext& Ctx) override;
};
