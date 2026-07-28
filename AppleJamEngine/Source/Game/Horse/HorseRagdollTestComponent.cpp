#include "HorseRagdollTestComponent.h"

#include "Animation/AnimationManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/Graph/AnimGraphInstance.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Component/AI/BTAgentComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Core/Logging/Log.h"
#include "Core/TickFunction.h"
#include "GameFramework/AActor.h"
#include "HorseLocomotionComponent.h"
#include "HorseMovementComponent.h"
#include "Math/Matrix.h"
#include "Math/Rotator.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/PhysicsAssetInstance.h"
#include "Serialization/Archive.h"

#include <algorithm>

#include <cmath>

namespace
{
	const FName RecoverLeftVarName  = FName("bRecoverLeft");
	const FName RecoverRightVarName = FName("bRecoverRight");

	// 대칭 본 쌍의 높이차를 쌍 간격으로 나눈 값(=roll 의 sin 근사)이 이보다 작으면
	// 두 본이 거의 겹쳐 있어 좌우 판정에 쓸 수 없다고 본다.
	constexpr float MinPairSeparation = 0.05f;   // m

	float RadToDeg(float Radians)
	{
		return Radians * (180.0f / 3.1415926535f);
	}

	// 물리 스냅샷에 아직 해당 바디가 없으면 GetBodyWorldTransformByBoneName 이 기본 FTransform 을
	// 돌려준다. 그대로 쓰면 월드 원점으로 순간이동하므로 "손대지 않은 transform" 은 무효 취급한다.
	bool IsUnpublishedBodyTransform(const FTransform& Transform)
	{
		return Transform.Location.IsNearlyZero() &&
			std::fabs(Transform.Rotation.W - 1.0f) < 1.0e-6f &&
			std::fabs(Transform.Rotation.X) < 1.0e-6f &&
			std::fabs(Transform.Rotation.Y) < 1.0e-6f &&
			std::fabs(Transform.Rotation.Z) < 1.0e-6f;
	}
}

void UHorseRagdollTestComponent::BeginPlay()
{
	Super::BeginPlay();

	bBegunPlay = true;
	bRagdollEnabled = false;
	Phase = EHorseRagdollTestPhase::Idle;
	ResolveMesh();

	if (!Mesh.Get())
	{
		UE_LOG("[HorseRagdollTest] SkeletalMeshComponent 를 찾지 못했다. Actor=%s", GetOwnerNameSafe());
	}
}

void UHorseRagdollTestComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << bSwapRecoverSide;
	Ar << RecoverFlagHoldTime;
	Ar << bAlignActorToRagdoll;
	Ar << bCompensateClipStartOffset;
	FString RecoverLeftPath  = RecoverLeftAnimPath.ToString();
	FString RecoverRightPath = RecoverRightAnimPath.ToString();
	Ar << RecoverLeftPath;
	Ar << RecoverRightPath;
	if (Ar.IsLoading())
	{
		RecoverLeftAnimPath.SetPath(RecoverLeftPath);
		RecoverRightAnimPath.SetPath(RecoverRightPath);
	}
	Ar << SideDecisionThreshold;
	Ar << bDefaultRecoverLeft;
	Ar << FallSideImpulse;
	Ar << PelvisBoneName;
	Ar << HeadBoneName;
	Ar << TorsoBoneName;
	Ar << FrontLeftBoneName;
	Ar << FrontRightBoneName;
	Ar << RearLeftBoneName;
	Ar << RearRightBoneName;
}

void UHorseRagdollTestComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	if (!bBegunPlay || !ResolveMesh())
	{
		return;
	}

	switch (Phase)
	{
	case EHorseRagdollTestPhase::Idle:
		if (bRagdollEnabled)
		{
			EnterRagdoll();
		}
		break;

	case EHorseRagdollTestPhase::Ragdoll:
		if (!bRagdollEnabled)
		{
			BeginRecover();
		}
		break;

	case EHorseRagdollTestPhase::Recovering:
		// 복귀 중의 토글은 무시한다. 복귀가 끝나 Idle 로 돌아간 뒤 다음 tick 에서 다시 평가된다.
		UpdateRecover(DeltaTime);
		break;
	}
}

void UHorseRagdollTestComponent::EnterRagdoll()
{
	USkeletalMeshComponent* MeshComp = Mesh.Get();
	if (!MeshComp)
	{
		bRagdollEnabled = false;
		return;
	}

	// 이전 복귀에서 남은 pulse 가 켜져 있으면 일어서는 클립으로 다시 들어간다.
	SetRecoverFlags(false, false);
	bRecoverFlagActive = false;
	RecoverFlagTimer = 0.0f;

	SuspendHorseControl();

	if (!MeshComp->EnableRagdollPhysics())
	{
		UE_LOG("[HorseRagdollTest] 래그돌 진입 실패 — PhysicsAsset 을 확인할 것. Actor=%s Mesh=%s",
			GetOwnerNameSafe(),
			MeshComp->GetName().c_str());
		ResumeHorseControl();
		bRagdollEnabled = false;
		return;
	}

	if (FallSideImpulse != 0.0f)
	{
		if (FPhysicsAssetInstance* Instance = GetPhysicsInstance())
		{
			const AActor* OwnerActor = GetOwner();
			const FVector Right = OwnerActor ? OwnerActor->GetActorRight() : FVector(0.0f, 1.0f, 0.0f);
			Instance->AddImpulseToBone(TorsoBoneName, Right * FallSideImpulse);
		}
	}

	Phase = EHorseRagdollTestPhase::Ragdoll;
	UE_LOG("[HorseRagdollTest] 래그돌 시작. Actor=%s Bodies=%d Constraints=%d",
		GetOwnerNameSafe(),
		MeshComp->GetLiveRagdollBodyCount(),
		MeshComp->GetLiveRagdollConstraintCount());
}

void UHorseRagdollTestComponent::BeginRecover()
{
	USkeletalMeshComponent* MeshComp = Mesh.Get();
	if (!MeshComp)
	{
		Phase = EHorseRagdollTestPhase::Idle;
		return;
	}

	// 바디가 살아있는 동안에만 읽을 수 있는 정보들 — 복귀 시작 전에 먼저 뽑는다.
	float LeftDownScore = 0.0f;
	const bool bHasSide = EvaluateLyingSide(LeftDownScore);
	const bool bDecisive = bHasSide && std::fabs(LeftDownScore) >= SideDecisionThreshold;
	bool bLyingOnLeft = bDecisive ? (LeftDownScore > 0.0f) : bDefaultRecoverLeft;
	if (bSwapRecoverSide)
	{
		bLyingOnLeft = !bLyingOnLeft;
	}
	CacheRestoreTransform();

	UE_LOG("[HorseRagdollTest] 복귀 시작. Actor=%s Side=%s Score=%.3f(Threshold=%.2f, %s) Swap=%s",
		GetOwnerNameSafe(),
		bLyingOnLeft ? "RecoverLeft" : "RecoverRight",
		LeftDownScore,
		SideDecisionThreshold,
		bDecisive ? "measured" : "fallback",
		bSwapRecoverSide ? "true" : "false");

	// AnimGraph 를 먼저 Recover 상태로 밀어넣어야, 빠져나오는 물리 포즈가 일어서는 클립의
	// 첫 포즈로 블렌드된다.
	SetRecoverFlags(bLyingOnLeft, !bLyingOnLeft);
	bRecoverFlagActive = true;
	RecoverFlagTimer = 0.0f;

	AlignActorToRagdoll(bLyingOnLeft);

	if (!MeshComp->BeginRagdollRecovery())
	{
		// 바디가 이미 정리된 경우 등 — 물리를 내리고 클립만 재생시킨다.
		MeshComp->DisableRagdollPhysics();
		UE_LOG("[HorseRagdollTest] BeginRagdollRecovery 거부됨 — 물리만 내리고 Recover 클립을 재생한다. Actor=%s",
			GetOwnerNameSafe());
	}

	// 쓰러지던 frame 에 쌓여 있다가 Movement tick 이 꺼지면서 소비되지 못한 root motion 이 남아 있을
	// 수 있다. 조종권을 돌려주기 전에 버린다 — 안 그러면 복귀 첫 frame 에 그만큼 튄다.
	if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
	{
		if (AnimInstance->HasPendingRootMotion())
		{
			AnimInstance->ConsumeRootMotion();
		}
	}

	// 일어서는 클립의 root motion 을 Movement 가 소비해야 하므로 여기서 조종권을 돌려준다.
	ResumeHorseControl();
	Phase = EHorseRagdollTestPhase::Recovering;
}

void UHorseRagdollTestComponent::UpdateRecover(float DeltaTime)
{
	USkeletalMeshComponent* MeshComp = Mesh.Get();

	if (bRecoverFlagActive)
	{
		RecoverFlagTimer += DeltaTime;
		if (RecoverFlagTimer >= RecoverFlagHoldTime)
		{
			SetRecoverFlags(false, false);
			bRecoverFlagActive = false;
		}
	}

	const bool bStillRecovering = MeshComp && (MeshComp->IsRecoveringFromRagdoll() || MeshComp->IsRagdollActive());
	if (bStillRecovering)
	{
		return;
	}

	if (bRecoverFlagActive)
	{
		SetRecoverFlags(false, false);
		bRecoverFlagActive = false;
	}

	Phase = EHorseRagdollTestPhase::Idle;
	UE_LOG("[HorseRagdollTest] 물리 블렌드 아웃 완료 — Recover 클립 재생 중. Actor=%s", GetOwnerNameSafe());
}

bool UHorseRagdollTestComponent::EvaluateLyingSide(float& OutLeftDownScore) const
{
	OutLeftDownScore = 0.0f;

	float Sum = 0.0f;
	int32 Count = 0;

	float Ratio = 0.0f;
	if (TryGetPairHeightRatio(FrontLeftBoneName, FrontRightBoneName, Ratio))
	{
		Sum += Ratio;
		++Count;
	}
	if (TryGetPairHeightRatio(RearLeftBoneName, RearRightBoneName, Ratio))
	{
		Sum += Ratio;
		++Count;
	}

	if (Count == 0)
	{
		return false;
	}

	OutLeftDownScore = Sum / static_cast<float>(Count);
	return true;
}

bool UHorseRagdollTestComponent::TryGetPairHeightRatio(const FName& LeftBone, const FName& RightBone, float& OutRatio) const
{
	FVector LeftLocation;
	FVector RightLocation;
	if (!TryGetBodyLocation(LeftBone, LeftLocation) || !TryGetBodyLocation(RightBone, RightLocation))
	{
		return false;
	}

	const FVector Delta = RightLocation - LeftLocation;
	const float Separation = Delta.Length();
	if (Separation < MinPairSeparation)
	{
		return false;
	}

	// 오른쪽이 위 → 왼쪽 옆구리가 바닥 → 양수.
	OutRatio = Delta.Z / Separation;
	return true;
}

bool UHorseRagdollTestComponent::TryGetBodyLocation(const FName& BoneName, FVector& OutLocation) const
{
	FPhysicsAssetInstance* Instance = GetPhysicsInstance();
	if (!Instance || !Instance->HasValidBodyForBone(BoneName))
	{
		return false;
	}

	const FTransform BodyTransform = Instance->GetBodyWorldTransformByBoneName(BoneName);
	if (IsUnpublishedBodyTransform(BodyTransform))
	{
		return false;
	}

	OutLocation = BodyTransform.Location;
	return true;
}

void UHorseRagdollTestComponent::CacheRestoreTransform()
{
	bHasCachedRestoreLocation = false;
	bHasCachedRestoreYaw = false;

	USkeletalMeshComponent* MeshComp = Mesh.Get();
	FVector RepresentativeLocation;
	if (!MeshComp || !MeshComp->TryGetRagdollRepresentativeLocation(RepresentativeLocation))
	{
		// 유효한 물리 포즈가 아직 한 번도 안 들어왔다 — 액터를 옮기지 않는 편이 안전하다.
		return;
	}

	FVector PelvisLocation;
	if (!TryGetBodyLocation(PelvisBoneName, PelvisLocation))
	{
		PelvisLocation = RepresentativeLocation;
	}

	CachedRestoreLocation = PelvisLocation;
	bHasCachedRestoreLocation = true;

	FVector HeadLocation;
	if (!TryGetBodyLocation(HeadBoneName, HeadLocation))
	{
		return;
	}

	FVector Facing = HeadLocation - PelvisLocation;
	Facing.Z = 0.0f;
	if (Facing.IsNearlyZero())
	{
		return;
	}

	CachedRestoreYaw = RadToDeg(std::atan2(Facing.Y, Facing.X));
	bHasCachedRestoreYaw = true;
}

void UHorseRagdollTestComponent::AlignActorToRagdoll(bool bRecoverLeftClip)
{
	AActor* OwnerActor = GetOwner();
	USkeletalMeshComponent* MeshComp = Mesh.Get();
	if (!OwnerActor || !bAlignActorToRagdoll || !bHasCachedRestoreLocation)
	{
		return;
	}

	// 클립 t=0 의 누운 포즈. 못 구하면 예전처럼 액터(=캡슐)를 래그돌 자리에 맞추는 데 그친다.
	FVector PelvisComponentSpace = FVector(0.0f, 0.0f, 0.0f);
	FVector FacingComponentSpace = FVector(0.0f, 0.0f, 0.0f);
	const bool bHasClipStartPose =
		bCompensateClipStartOffset &&
		MeshComp &&
		TryGetRecoverClipStartPose(bRecoverLeftClip, PelvisComponentSpace, FacingComponentSpace);

	if (bHasCachedRestoreYaw)
	{
		FRotator Rotation = OwnerActor->GetActorRotation();
		Rotation.Yaw = CachedRestoreYaw;
		OwnerActor->SetActorRotation(Rotation);

		// 클립의 누운 몸통 방향은 액터 forward 와 다르다. 액터를 Δ 만큼 더 돌리면 클립 방향도 정확히
		// Δ 만큼 돌아가므로(둘 다 Z 축 회전), 한 번 재보고 차이만큼 보정하면 끝난다.
		if (bHasClipStartPose && !FacingComponentSpace.IsNearlyZero())
		{
			const FQuat MeshRotation = MeshComp->GetWorldRotation().ToQuaternion().GetNormalized();
			FVector ClipFacingWorld = MeshRotation.RotateVector(FacingComponentSpace);
			ClipFacingWorld.Z = 0.0f;
			if (!ClipFacingWorld.IsNearlyZero())
			{
				const float ClipFacingYaw = RadToDeg(std::atan2(ClipFacingWorld.Y, ClipFacingWorld.X));
				Rotation.Yaw += CachedRestoreYaw - ClipFacingYaw;
				OwnerActor->SetActorRotation(Rotation);
			}
		}
	}

	if (!bHasClipStartPose)
	{
		OwnerActor->SetActorLocation(CachedRestoreLocation);
		return;
	}

	// 회전이 확정된 뒤에 계산해야 한다 — mesh world matrix 가 액터 회전을 포함한다.
	const FVector PredictedPelvisWorld = MeshComp->GetWorldMatrix().TransformPositionWithW(PelvisComponentSpace);
	const FVector Correction = CachedRestoreLocation - PredictedPelvisWorld;
	OwnerActor->SetActorLocation(OwnerActor->GetActorLocation() + Correction);

	UE_LOG("[HorseRagdollTest] 클립 시작 포즈 정렬. Actor=%s ClipPelvisCS=(%.2f,%.2f,%.2f) Correction=(%.2f,%.2f,%.2f)",
		GetOwnerNameSafe(),
		PelvisComponentSpace.X,
		PelvisComponentSpace.Y,
		PelvisComponentSpace.Z,
		Correction.X,
		Correction.Y,
		Correction.Z);
}

bool UHorseRagdollTestComponent::TryGetRecoverClipStartPose(
	bool bRecoverLeftClip,
	FVector& OutPelvisComponentSpace,
	FVector& OutFacingComponentSpace) const
{
	USkeletalMeshComponent* MeshComp = Mesh.Get();
	USkeletalMesh* SkeletalMeshAsset = MeshComp ? MeshComp->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* MeshData = SkeletalMeshAsset ? SkeletalMeshAsset->GetSkeletalMeshAsset() : nullptr;
	if (!MeshData || MeshData->Bones.empty())
	{
		return false;
	}

	const FSoftObjectPtr& ClipPath = bRecoverLeftClip ? RecoverLeftAnimPath : RecoverRightAnimPath;
	if (ClipPath.IsNull())
	{
		return false;
	}

	UAnimSequence* Clip = FAnimationManager::Get().LoadAnimation(ClipPath.ToString());
	TArray<FTransform> LocalPose;
	if (!Clip || !Clip->GetAnimationPose(0.0f, SkeletalMeshAsset, LocalPose, false))
	{
		UE_LOG("[HorseRagdollTest] 회복 클립 포즈를 읽지 못했다 — 클립 시작 오프셋 보정 생략. Clip=%s",
			ClipPath.ToString().c_str());
		return false;
	}

	// USkinnedMeshComponent::BuildBoneEditGlobalMatrices 와 동일한 누적식(row-vector,
	// Global = Local * ParentGlobal). 본 순서는 parent-first 전제.
	const int32 BoneCount = std::min(static_cast<int32>(MeshData->Bones.size()), static_cast<int32>(LocalPose.size()));
	if (BoneCount <= 0)
	{
		return false;
	}

	TArray<FMatrix> GlobalMatrices;
	GlobalMatrices.resize(BoneCount);
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FMatrix LocalMatrix = LocalPose[BoneIndex].ToMatrix();
		const int32 ParentIndex = MeshData->Bones[BoneIndex].ParentIndex;
		GlobalMatrices[BoneIndex] = (ParentIndex >= 0 && ParentIndex < BoneIndex)
			? LocalMatrix * GlobalMatrices[ParentIndex]
			: LocalMatrix;
	}

	auto GetBoneComponentSpaceLocation = [&](const FName& BoneName, FVector& OutLocation) -> bool
	{
		const int32 BoneIndex = MeshComp->FindBoneIndex(BoneName);
		if (BoneIndex < 0 || BoneIndex >= BoneCount)
		{
			return false;
		}
		const FMatrix& Global = GlobalMatrices[BoneIndex];
		OutLocation = FVector(Global.M[3][0], Global.M[3][1], Global.M[3][2]);
		return true;
	};

	if (!GetBoneComponentSpaceLocation(PelvisBoneName, OutPelvisComponentSpace))
	{
		return false;
	}

	OutFacingComponentSpace = FVector(0.0f, 0.0f, 0.0f);
	FVector HeadComponentSpace;
	if (GetBoneComponentSpaceLocation(HeadBoneName, HeadComponentSpace))
	{
		OutFacingComponentSpace = HeadComponentSpace - OutPelvisComponentSpace;
		OutFacingComponentSpace.Z = 0.0f;
	}

	return true;
}

void UHorseRagdollTestComponent::SetRecoverFlags(bool bLeft, bool bRight)
{
	UAnimGraphInstance* Graph = GetGraph();
	if (!Graph)
	{
		return;
	}

	Graph->SetGraphVariableBool(RecoverLeftVarName, bLeft);
	Graph->SetGraphVariableBool(RecoverRightVarName, bRight);
}

void UHorseRagdollTestComponent::SuspendHorseControl()
{
	if (bControlSuspended)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	if (UHorseMovementComponent* Movement = OwnerActor->GetComponentByClass<UHorseMovementComponent>())
	{
		bSavedMovementTick = Movement->PrimaryComponentTick.bTickEnabled;
		Movement->SetComponentTickEnabled(false);
	}
	if (UHorseLocomotionComponent* Locomotion = OwnerActor->GetComponentByClass<UHorseLocomotionComponent>())
	{
		bSavedLocomotionTick = Locomotion->PrimaryComponentTick.bTickEnabled;
		Locomotion->SetComponentTickEnabled(false);
	}
	if (UBTAgentComponent* BTAgent = OwnerActor->GetComponentByClass<UBTAgentComponent>())
	{
		bSavedBTAgentTick = BTAgent->PrimaryComponentTick.bTickEnabled;
		BTAgent->SetComponentTickEnabled(false);
	}

	// 몸통 캡슐은 kinematic 이라 래그돌 바디를 걷어차 버린다. 누워 있는 동안만 내려둔다.
	if (UCapsuleComponent* Capsule = OwnerActor->GetComponentByClass<UCapsuleComponent>())
	{
		SuspendedCapsule = Capsule;
		SavedCapsuleCollision = Capsule->GetCollisionEnabled();
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	bControlSuspended = true;
}

void UHorseRagdollTestComponent::ResumeHorseControl()
{
	if (!bControlSuspended)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
	{
		if (UHorseMovementComponent* Movement = OwnerActor->GetComponentByClass<UHorseMovementComponent>())
		{
			Movement->SetComponentTickEnabled(bSavedMovementTick);
		}
		if (UHorseLocomotionComponent* Locomotion = OwnerActor->GetComponentByClass<UHorseLocomotionComponent>())
		{
			Locomotion->SetComponentTickEnabled(bSavedLocomotionTick);
		}
		if (UBTAgentComponent* BTAgent = OwnerActor->GetComponentByClass<UBTAgentComponent>())
		{
			BTAgent->SetComponentTickEnabled(bSavedBTAgentTick);
		}
	}

	if (UCapsuleComponent* Capsule = SuspendedCapsule.Get())
	{
		Capsule->SetCollisionEnabled(SavedCapsuleCollision);
	}
	SuspendedCapsule = nullptr;

	bControlSuspended = false;
}

USkeletalMeshComponent* UHorseRagdollTestComponent::ResolveMesh()
{
	USkeletalMeshComponent* Cached = Mesh.Get();
	if (Cached && Cached->GetOwner() == GetOwner())
	{
		return Cached;
	}

	AActor* OwnerActor = GetOwner();
	Mesh = OwnerActor ? OwnerActor->GetComponentByClass<USkeletalMeshComponent>() : nullptr;
	return Mesh.Get();
}

FPhysicsAssetInstance* UHorseRagdollTestComponent::GetPhysicsInstance() const
{
	USkeletalMeshComponent* MeshComp = Mesh.Get();
	return MeshComp ? MeshComp->GetPhysicsAssetInstance() : nullptr;
}

UAnimGraphInstance* UHorseRagdollTestComponent::GetGraph() const
{
	USkeletalMeshComponent* MeshComp = Mesh.Get();
	return MeshComp ? Cast<UAnimGraphInstance>(MeshComp->GetAnimInstance()) : nullptr;
}

const char* UHorseRagdollTestComponent::GetOwnerNameSafe() const
{
	if (AActor* OwnerActor = GetOwner())
	{
		return OwnerActor->GetName().c_str();
	}
	return "None";
}
