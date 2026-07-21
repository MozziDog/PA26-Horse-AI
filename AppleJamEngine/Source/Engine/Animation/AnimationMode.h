#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/Ptr/ObjectPtr.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Object/Reflection/UStruct.h"

#include "Source/Engine/Animation/AnimationMode.generated.h"

class UAnimSequenceBase;

// USkinnedMeshComponent 가 AnimInstance 를 어떤 방식으로 관리할지 결정한다.
// UE 의 EAnimationMode 와 의미 동일 — Blueprint 자리가 우리 환경엔 없으므로
// AnimationCustom 으로 의역 (= 사용자가 UAnimInstance 자식 클래스를 지정).
UENUM()
enum class EAnimationMode : uint8
{
	None,                  // 애니메이션 없음. 컴포넌트의 BoneEdit 만 동작.
	AnimationSingleNode,   // 시퀀스 1개 재생. AnimationData 의 AnimToPlay 사용.
	AnimationCustom,       // AnimInstanceClass 로 지정한 UAnimInstance 자식 인스턴스화 (FSM 등).
};

// EditorPropertyWidget Enum 콤보용 표시 이름. EAnimationMode 와 1:1 순서.
inline const char* GAnimationModeNames[] = {
	"None",
	"AnimationSingleNode",
	"AnimationCustom",
};
inline constexpr uint32 GAnimationModeCount = sizeof(GAnimationModeNames) / sizeof(GAnimationModeNames[0]);

// AnimInstance 가 어떤 소스에서 root motion 을 누적할지 결정.
// UE 의 ERootMotionMode 와 의미 동일. 본가는 SkeletalMeshComponent 에 박지만,
// 우리 구조에선 AnimInstance 가 누적 buffer + Montage 보유 + base/FSM 누적 분기를
// 모두 들고 있어 정책도 같은 클래스에 두는 게 일관성 있음.
//
// 누적 책임 매트릭스:
//   IgnoreRootMotion          — Base/Montage 모두 누적 안 함 (Consume 시 항상 identity)
//   RootMotionFromEverything  — Base (SingleNode/FSM) + Montage 둘 다 누적
//   RootMotionFromMontagesOnly — Montage 만 누적, Base 는 skip
//
// 현재 default 는 RootMotionFromEverything — 기존 동작 (항상 누적) 과 동일하므로 회귀 0.
UENUM()
enum class ERootMotionMode : uint8
{
	IgnoreRootMotion,
	RootMotionFromEverything,
	RootMotionFromMontagesOnly,
};

inline const char* GRootMotionModeNames[] = {
	"IgnoreRootMotion",
	"RootMotionFromEverything",
	"RootMotionFromMontagesOnly",
};
inline constexpr uint32 GRootMotionModeCount = sizeof(GRootMotionModeNames) / sizeof(GRootMotionModeNames[0]);

// Root motion 클립에서 rotation 을 어디까지 "이동" 으로 추출하고 pose 에서 잠글지 — per-asset
// (UAnimSequence, bExtractRootMotionZ 와 같은 그룹). "pose 에서 제거한 성분 = delta 로 추출되는
// 성분" 불변식을 클립 단위로 보장하므로 소비자가 무엇을 적용하든 이중 적용/유실이 없다.
//
//   Full    — rotation 전체를 추출하고 첫 키로 잠금 (default = 기존 동작. 몽타주/휴머노이드).
//   YawOnly — up(+Z)축 yaw 만 추출+잠금. swing(pitch/roll) 은 pose 에 애니메이션 절대값 유지
//             (클립 고유 기울기·흔들림이 상태 블렌드에 자연스럽게 섞임). 보행류 클립.
//   None    — rotation 추출 안 함 (translation-only root motion). pose 에 회전 전체 유지.
enum class ERootMotionRotationLock : uint8
{
	Full,
	YawOnly,
	None,
};

// Editor 콤보용 표시 이름. ERootMotionRotationLock 과 1:1 순서.
inline const char* GRootMotionRotationLockNames[] = {
	"Full",
	"YawOnly",
	"None",
};
inline constexpr uint32 GRootMotionRotationLockCount = sizeof(GRootMotionRotationLockNames) / sizeof(GRootMotionRotationLockNames[0]);

// SingleNode 모드에서 직렬화/에디터 노출용으로 묶어 두는 재생 파라미터.
// AnimToPlay 는 런타임 포인터, AnimToPlayPath 는 직렬화/에디터용 식별자 (= asset 경로).
// SetAnimation 등 설정 경로는 두 멤버를 항상 동기화한다.
USTRUCT()
struct FSingleAnimationPlayData
{
	GENERATED_BODY()

	TObjectPtr<UAnimSequenceBase> AnimToPlay;

	UPROPERTY(Edit, Save, Category="Animation", DisplayName="Anim To Play", AssetType="UAnimSequence")
	FSoftObjectPtr     AnimToPlayPath;
	UPROPERTY(Edit, Save, Category="Animation", DisplayName="Play Rate", Min=-4.0f, Max=4.0f, Speed=0.05f)
	float              PlayRate       = 1.0f;
	UPROPERTY(Edit, Save, Category="Animation", DisplayName="Looping")
	bool               bLooping       = true;
	UPROPERTY(Edit, Save, Category="Animation", DisplayName="Playing")
	bool               bPlaying       = true;
};
