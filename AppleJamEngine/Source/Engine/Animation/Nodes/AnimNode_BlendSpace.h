#pragma once

#include "AnimNode_Base.h"
#include "Animation/Graph/BlendSpaceTriangulation.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

class FAnimNode_SequencePlayer;

// 2D(및 1D 퇴화) Blend Space 노드 — 임의 산점 샘플을 축값 (AxisX, AxisY) 에 따라 연속 블렌드.
//   Initialize : 샘플별 내부 SequencePlayer init + 삼각망(BlendSpaceTriangulation) build.
//   Update     : 축값(AxisXFn/AxisYFn) → 질의점 → 활성 샘플/가중치 계산. 모든 샘플 player 를
//                동일 dt 로 진행(보법 내 클립 길이 동일 전제 → 위상 자동 정렬, sync 인프라 불필요,
//                설계 §2-5). Notify/RM 은 활성 가중치만 반영. 가중 RM lerp.
//   Evaluate   : 활성 pose N-way 가중 블렌드(FAnimationRuntime::BlendPosesTogether).
//
// 축 바인딩: 컴파일러가 X/Y Float 입력 핀의 source(VariableGet→Float Variable)를 MakeFloatReader
// 람다로 inline 해 AxisXFn/AxisYFn 에 주입. 미연결 축은 함수 미설정 → 0 으로 평가 → 그 축에서 모든
// 샘플 동일 → 자동 1D 퇴화(설계 §2-6, §2-7). 삼각망 build 도 좌표가 공선이면 1D 로 흡수.
//
// Root motion 은 외부 누적 패턴 — 자기 LastRootMotionDelta 만 채우고 직접 AccumulateRootMotion
// 호출 X. UAnimState::SubGraphOverride 로 꽂히면 State 가 이 값을 mirror 해 부모 SM 에 전달.
class FAnimNode_BlendSpace : public FAnimNode_Base
{
public:
	// ── Build-side (컴파일러가 채움) ──
	TArray<FAnimNode_SequencePlayer*> SamplePlayers; // 샘플별 내부 재생 노드. OwnedNodes 가 소유.
	TArray<FVector2>                  SampleCoords;   // 샘플 2D 좌표(SamplePlayers 와 index 일치).

	TFunction<float(UAnimInstance*)>  AxisXFn;        // 미설정 → 0.
	TFunction<float(UAnimInstance*)>  AxisYFn;        // 미설정 → 0.

	// ── Runtime state ──
	FBlendSpaceTriangulation          Triangulation;  // Initialize 시 build, 이후 read-only.
	TArray<FBlendTriangulationWeight> ActiveWeights;  // 이번 frame 활성 샘플/가중치(Update→Evaluate 공유).
	FTransform                        LastRootMotionDelta;

	void Initialize(const FAnimationInitializeContext& Context) override;
	void OnBecomeRelevant(const FAnimationInitializeContext& Context) override;
	void OnDormant() override;
	void Update(const FAnimationUpdateContext& Context) override;
	void Evaluate(FPoseContext& Output) override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;
	const FTransform& GetLastRootMotionDelta() const override { return LastRootMotionDelta; }

	const char* GetDebugName() const override { return "BlendSpace"; }

private:
	// 현재 축값으로 질의점 구성(미설정 축 = 0).
	FVector2 EvaluateQuery(UAnimInstance* AnimInstance) const;
};
