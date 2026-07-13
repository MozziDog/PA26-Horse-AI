#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"

struct FPoseContext;
struct FMatrix;

// 포즈 연산 정적 라이브러리. UObject 가 아니다.
namespace FAnimationRuntime
{
	// N 개 포즈를 가중치로 합성. BlendTwoPosesTogether 의 incremental 합성 —
	//   acc = Poses[0];  for k>=1: acc = Blend(acc, Poses[k], Wk/(AccW+Wk))
	// 로 가중 평균을 만든다(가중치 합이 1 이 아니어도 내부에서 정규화). BlendSpace N-way 블렌드용.
	//   - Poses / Weights 크기 동일. 유효(가중치>0) 항목이 없으면 Out 은 첫 포즈(또는 ref pose) 로.
	//   - Out 은 Poses[k] 중 하나와 같은 인스턴스가 아니어야 한다(별도 버퍼).
	void BlendPosesTogether(
		const TArray<const FPoseContext*>& Poses,
		const TArray<float>&               Weights,
		FPoseContext&                      Out);

	// 본별 transform 을 선형 보간.
	//   Location/Scale → Lerp
	//   Rotation       → Slerp (★ 사원수 Lerp 는 잘못된 방향 보간 — 반드시 Slerp)
	//
	// A.Pose.size() == B.Pose.size() == Out.Pose.size() 가정.
	// Alpha 0 → A, 1 → B, [0,1] 외 범위는 호출자 책임.
	void BlendTwoPosesTogether(
		const FPoseContext& A,
		const FPoseContext& B,
		float Alpha,
		FPoseContext& Out);

	// 본 로컬 행렬 → FTransform 분해. row-major 가정. row 별 scale 을 제거한 뒤 회전을 추출.
	// FBone::LocalMatrix 같은 bind-pose 행렬을 Animation 자료구조 (FTransform) 로 옮길 때 사용.
	FTransform DecomposeMatrix(const FMatrix& Mat);
}
