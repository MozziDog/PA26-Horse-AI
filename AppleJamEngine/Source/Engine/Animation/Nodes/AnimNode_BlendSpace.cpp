#include "AnimNode_BlendSpace.h"

#include "AnimNode_SequencePlayer.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationRuntime.h"
#include "Animation/PoseContext.h"
#include "Math/Quat.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"

#include <algorithm>

void FAnimNode_BlendSpace::Initialize(const FAnimationInitializeContext& Context)
{
	// 샘플별 내부 player init — 자식 깊이 init 보장(BlendListByEnum 과 동일).
	for (FAnimNode_SequencePlayer* Player : SamplePlayers)
	{
		if (Player) Player->Initialize(Context);
	}

	// 삼각망 build — 좌표 고정, 이후 read-only. 공선/2/1샘플이면 내부에서 1D 퇴화 처리.
	Triangulation.Build(SampleCoords);
	ActiveWeights.clear();
	LastRootMotionDelta = FTransform();
}

void FAnimNode_BlendSpace::OnBecomeRelevant(const FAnimationInitializeContext& Context)
{
	// State 진입 등으로 활성화될 때 모든 샘플 위상 정렬(LocalTime reset). 보법 내 클립 길이 동일
	// 전제에서 이 정렬이 유지되므로 이후 sync 인프라 없이 방향 블렌드가 깨끗.
	for (FAnimNode_SequencePlayer* Player : SamplePlayers)
	{
		if (Player) Player->OnBecomeRelevant(Context);
	}
}

void FAnimNode_BlendSpace::OnDormant()
{
	for (FAnimNode_SequencePlayer* Player : SamplePlayers)
	{
		if (Player) Player->OnDormant();
	}
	ActiveWeights.clear();
}

FVector2 FAnimNode_BlendSpace::EvaluateQuery(UAnimInstance* AnimInstance) const
{
	// 미설정 축은 0 → 1D 퇴화(설계 §2-7). MakeFloatReader 도 None/미발견 시 0 반환.
	const float X = AxisXFn ? AxisXFn(AnimInstance) : 0.0f;
	const float Y = AxisYFn ? AxisYFn(AnimInstance) : 0.0f;
	return FVector2(X, Y);
}

void FAnimNode_BlendSpace::Update(const FAnimationUpdateContext& Context)
{
	const int32 NumSamples = static_cast<int32>(SamplePlayers.size());
	if (NumSamples == 0)
	{
		ActiveWeights.clear();
		LastRootMotionDelta = FTransform();
		return;
	}

	// 1) 축값 → 질의점 → 활성 샘플/가중치.
	const FVector2 Query = EvaluateQuery(Context.AnimInstance);
	Triangulation.CalculateWeights(Query, ActiveWeights);

	// 샘플 인덱스별 가중치 lookup (기본 0).
	TArray<float> Weight(NumSamples, 0.0f);
	for (const FBlendTriangulationWeight& W : ActiveWeights)
	{
		if (W.SampleIndex >= 0 && W.SampleIndex < NumSamples)
		{
			Weight[W.SampleIndex] += W.Weight;
		}
	}

	// 2) 모든 샘플 player 를 동일 dt 로 진행 — 위상 정렬 유지(길이 동일 전제). Notify 는 활성
	//    가중치가 임계 이상일 때만 발사(SequencePlayer 가 FinalBlendWeight 로 가드).
	const float ParentW = Context.FinalBlendWeight;
	for (int32 i = 0; i < NumSamples; ++i)
	{
		if (!SamplePlayers[i]) continue;
		FAnimationUpdateContext ChildCtx = Context;
		ChildCtx.FinalBlendWeight = ParentW * Weight[i];
		SamplePlayers[i]->Update(ChildCtx);
	}

	// 3) 가중 root motion — 활성 샘플들의 LastRM 을 가중 합성(외부 누적 X). 위치는 가중 합,
	//    회전은 incremental slerp 로 가중 평균.
	FTransform AccRM;
	bool  bHasRM = false;
	float AccW   = 0.0f;
	for (const FBlendTriangulationWeight& W : ActiveWeights)
	{
		if (W.SampleIndex < 0 || W.SampleIndex >= NumSamples || !SamplePlayers[W.SampleIndex]) continue;
		if (W.Weight <= 0.0f) continue;
		const FTransform& RM = SamplePlayers[W.SampleIndex]->GetLastRootMotionDelta();
		if (!bHasRM)
		{
			AccRM  = RM;
			AccW   = W.Weight;
			bHasRM = true;
		}
		else
		{
			const float NewW  = AccW + W.Weight;
			const float Alpha = (NewW > 1e-8f) ? (W.Weight / NewW) : 0.0f;
			AccRM.Location = AccRM.Location * (1.0f - Alpha) + RM.Location * Alpha;
			AccRM.Rotation = FQuat::Slerp(AccRM.Rotation.GetNormalized(), RM.Rotation.GetNormalized(), Alpha).GetNormalized();
			AccRM.Scale    = AccRM.Scale * (1.0f - Alpha) + RM.Scale * Alpha;
			AccW = NewW;
		}
	}
	LastRootMotionDelta = bHasRM ? AccRM : FTransform();
}

void FAnimNode_BlendSpace::Evaluate(FPoseContext& Output)
{
	const int32 NumSamples = static_cast<int32>(SamplePlayers.size());
	if (NumSamples == 0 || ActiveWeights.empty())
	{
		Output.ResetToRefPose();
		return;
	}

	// 활성 1개 — 그 player 그대로 출력(불필요한 블렌드 회피).
	if (ActiveWeights.size() == 1)
	{
		const int32 Idx = ActiveWeights[0].SampleIndex;
		if (Idx >= 0 && Idx < NumSamples && SamplePlayers[Idx])
		{
			SamplePlayers[Idx]->Evaluate(Output);
		}
		else
		{
			Output.ResetToRefPose();
		}
		return;
	}

	// N-way 가중 블렌드 — 각 활성 샘플 pose 평가 후 BlendPosesTogether.
	// PoseContext 는 로컬 버퍼로 보관(포인터 안정성 위해 미리 크기 확보).
	TArray<FPoseContext>        Buffers(ActiveWeights.size());
	TArray<const FPoseContext*> Poses;
	TArray<float>               Weights;
	Poses.reserve(ActiveWeights.size());
	Weights.reserve(ActiveWeights.size());

	for (size_t k = 0; k < ActiveWeights.size(); ++k)
	{
		const int32 Idx = ActiveWeights[k].SampleIndex;
		Buffers[k].SkeletalMesh = Output.SkeletalMesh;
		Buffers[k].ResetToRefPose();
		if (Idx >= 0 && Idx < NumSamples && SamplePlayers[Idx])
		{
			SamplePlayers[Idx]->Evaluate(Buffers[k]);
		}
		Poses.push_back(&Buffers[k]);
		Weights.push_back(ActiveWeights[k].Weight);
	}

	FAnimationRuntime::BlendPosesTogether(Poses, Weights, Output);
}

void FAnimNode_BlendSpace::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FAnimNode_SequencePlayer* Player : SamplePlayers)
	{
		if (Player) Player->AddReferencedObjects(Collector);
	}
}
