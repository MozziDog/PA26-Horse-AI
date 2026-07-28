#pragma once

#include "Component/ActorComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Vector.h"
#include "Object/FName.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Game/Horse/HorseRagdollTestComponent.generated.h"

class FPhysicsAssetInstance;
class UAnimGraphInstance;
class UCapsuleComponent;
class USkeletalMeshComponent;

UENUM()
enum class EHorseRagdollTestPhase : uint8
{
	Idle,        // 평상시 — AnimGraph 가 포즈를 만든다
	Ragdoll,     // 물리 구동 — 쓰러지는 중 / 누워 있음
	Recovering,  // 물리→애니메이션 블렌드 + H_Recover 클립 재생 중
};

// 말 래그돌 테스트용 컴포넌트. property panel 의 "Ragdoll Enabled" 체크박스 하나로 켜고 끈다.
//   체크 ON  → 전신 래그돌 시작. Movement/Locomotion/BT tick 과 몸통 캡슐 콜리전을 잠시 내려
//              래그돌 바디가 자기 캡슐에 차이지 않게 한다.
//   체크 OFF → 누운 방향을 판정해서 AnimGraph 의 bRecoverLeft / bRecoverRight 중 하나를 켜고,
//              물리 포즈를 애니메이션 포즈로 블렌드 아웃(USkeletalMeshComponent::BeginRagdollRecovery).
//              액터는 래그돌이 멈춘 위치/방향으로 옮겨진 뒤 Movement 가 다시 살아난다.
//
// 누운 방향 판정은 rig 의 본 축 방향에 의존하지 않도록, 좌/우 대칭 본 쌍의 높이차로만 구한다.
// (왼발이 오른발보다 낮으면 왼쪽으로 누운 것 → bRecoverLeft). 클립의 좌/우 의미가 반대라면
// "Swap Recover Side" 로 뒤집는다.
//
// AnimGraph 계약: bRecoverLeft / bRecoverRight 는 AnyState → Recover_* 전이 조건이라
// 계속 true 로 두면 클립이 끝나고 Stop 으로 빠진 직후 다시 재진입한다. bJump / bRearing 과 같은
// pulse 취급 — Recover Flag Hold 동안만 켜뒀다가 내린다.
UCLASS()
class UHorseRagdollTestComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UHorseRagdollTestComponent() = default;
	~UHorseRagdollTestComponent() override = default;

	void BeginPlay() override;
	void Serialize(FArchive& Ar) override;

	UFUNCTION(Pure, Category="Horse|Ragdoll Test")
	bool IsRagdolling() const { return Phase != EHorseRagdollTestPhase::Idle; }

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void EnterRagdoll();
	void BeginRecover();
	void UpdateRecover(float DeltaTime);

	// 좌/우 대칭 본 쌍의 높이차(쌍 간격으로 정규화 = roll 의 sin 근사)를 평균낸다.
	// 반환 > 0 이면 왼쪽이 아래 = 왼쪽으로 누움. 유효한 쌍이 하나도 없으면 false.
	bool EvaluateLyingSide(float& OutLeftDownScore) const;
	bool TryGetPairHeightRatio(const FName& LeftBone, const FName& RightBone, float& OutRatio) const;
	bool TryGetBodyLocation(const FName& BoneName, FVector& OutLocation) const;

	// 래그돌이 멈춘 위치(Pelvis)와 바라보는 방향(Pelvis→Head 수평 성분)을 잡아둔다.
	void CacheRestoreTransform();
	// 액터가 아니라 "회복 클립 t=0 의 누운 포즈" 가 래그돌 위에 얹히도록 액터를 배치한다.
	void AlignActorToRagdoll(bool bRecoverLeftClip);
	// 회복 클립 첫 프레임의 Pelvis component-space 위치와 몸통 수평 방향(Pelvis→Head).
	bool TryGetRecoverClipStartPose(bool bRecoverLeftClip, FVector& OutPelvisComponentSpace, FVector& OutFacingComponentSpace) const;

	void SetRecoverFlags(bool bLeft, bool bRight);
	// Movement/Locomotion/BT tick 과 몸통 캡슐 콜리전 on/off. 원래 값은 Suspend 시점에 저장한다.
	void SuspendHorseControl();
	void ResumeHorseControl();

	USkeletalMeshComponent* ResolveMesh();
	FPhysicsAssetInstance*  GetPhysicsInstance() const;
	UAnimGraphInstance*     GetGraph() const;
	const char*             GetOwnerNameSafe() const;

private:
	UPROPERTY(Edit, Category="Horse|Ragdoll Test", DisplayName="Ragdoll Enabled")
	bool bRagdollEnabled = false;   // 이 체크박스가 유일한 조작부. Save 안 함 — 씬 로드는 항상 Idle 로 시작

	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test", DisplayName="Swap Recover Side")
	bool bSwapRecoverSide = false;  // H_Recover Left/Right 의 좌우 의미가 판정과 반대일 때 뒤집기

	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test", DisplayName="Recover Flag Hold", Min=0.05f, Max=2.0f, Speed=0.01f)
	float RecoverFlagHoldTime = 0.35f;   // 초 — bRecoverLeft/Right 를 켜두는 시간(클립 길이보다 짧아야 함)

	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test", DisplayName="Align Actor To Ragdoll")
	bool bAlignActorToRagdoll = true;    // 복귀 시작 시 액터를 래그돌이 멈춘 위치/방향으로 이동

	// H_Recover 클립은 root motion 을 쓰지 않고(bEnableRootMotion=false) 누운→선 이동 전체가 포즈에
	// 들어있다. 즉 클립 원점은 "일어선 뒤" 위치라, 액터를 래그돌 자리에 그냥 놓으면 t=0 의 누운 포즈가
	// 클립상의 (누운 위치 - 선 위치) 만큼 어긋난다. 이 옵션이 그 오프셋을 액터 배치에서 빼준다.
	// (끄면 예전 동작 = 캡슐을 래그돌 자리에 맞춤 — 오차 비교용)
	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test", DisplayName="Compensate Clip Start Offset")
	bool bCompensateClipStartOffset = true;

	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test|Clips", DisplayName="Recover Left Animation", AssetType="UAnimSequence")
	FSoftObjectPtr RecoverLeftAnimPath = "Content/Mesh/Horse/Reexport/H_Recover Left_Scene.uasset";
	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test|Clips", DisplayName="Recover Right Animation", AssetType="UAnimSequence")
	FSoftObjectPtr RecoverRightAnimPath = "Content/Mesh/Horse/Reexport/H_Recover Right_Scene.uasset";

	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test", DisplayName="Side Decision Threshold", Min=0.0f, Max=1.0f, Speed=0.01f)
	float SideDecisionThreshold = 0.15f; // 이 값 미만이면 배/등으로 누운 애매한 자세 → Default Recover Left 사용

	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test", DisplayName="Default Recover Left")
	bool bDefaultRecoverLeft = true;     // 좌우 판정이 애매할 때 쓸 기본값

	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test", DisplayName="Fall Side Impulse", Min=-50.0f, Max=50.0f, Speed=0.1f)
	float FallSideImpulse = 0.0f;        // 래그돌 진입 순간 몸통에 주는 횡방향 임펄스(+우측). 0 이면 미적용
	                                     // 옆으로 쓰러지는 유도는 physics asset constraint 가 담당 — 이건 확인용 보조 손잡이

	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test|Bones", DisplayName="Pelvis Bone")
	FName PelvisBoneName = FName("Pelvis");
	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test|Bones", DisplayName="Head Bone")
	FName HeadBoneName = FName("Head");
	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test|Bones", DisplayName="Torso Bone")
	FName TorsoBoneName = FName("Spine2");
	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test|Bones", DisplayName="Front Left Bone")
	FName FrontLeftBoneName = FName("L Forearm");
	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test|Bones", DisplayName="Front Right Bone")
	FName FrontRightBoneName = FName("R Forearm");
	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test|Bones", DisplayName="Rear Left Bone")
	FName RearLeftBoneName = FName("L Calf");
	UPROPERTY(Edit, Save, Category="Horse|Ragdoll Test|Bones", DisplayName="Rear Right Bone")
	FName RearRightBoneName = FName("R Calf");

	EHorseRagdollTestPhase Phase = EHorseRagdollTestPhase::Idle;
	float RecoverFlagTimer = 0.0f;
	bool  bRecoverFlagActive = false;
	bool  bBegunPlay = false;

	TWeakObjectPtr<USkeletalMeshComponent> Mesh = nullptr;

	// ── Suspend 시점 스냅샷 ──
	TWeakObjectPtr<UCapsuleComponent> SuspendedCapsule = nullptr;
	ECollisionEnabled SavedCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
	bool  bSavedMovementTick = true;
	bool  bSavedLocomotionTick = true;
	bool  bSavedBTAgentTick = true;
	bool  bControlSuspended = false;

	FVector CachedRestoreLocation = FVector(0.0f, 0.0f, 0.0f);
	float   CachedRestoreYaw = 0.0f;
	bool    bHasCachedRestoreLocation = false;
	bool    bHasCachedRestoreYaw = false;
};
