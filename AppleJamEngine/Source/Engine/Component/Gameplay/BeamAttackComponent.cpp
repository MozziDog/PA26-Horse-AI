#include "Component/Gameplay/BeamAttackComponent.h"

#include "Component/Gameplay/BulletHellDamageReceiverComponent.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Logging/Log.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/GameMode/GameplayStatics.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "Math/Quat.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"
#include "Particle/ParticleSystemManager.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/TypeData/ParticleModuleTypeDataBeam.h"
#include "Render/Types/MinimalViewInfo.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float RadToDeg = 57.2957795131f;
	constexpr const char* BossTagName = "Boss";

	// Beam(Distance 방식)은 로컬 +X 로 뻗으므로, 빔이 Dir(카메라 시선) 방향으로 진행하도록
	// 컴포넌트 world rotation 을 forward(+X)==Dir 로 맞춘다.
	// FRotator::GetForwardVector 규약: Forward=(cosP*cosY, cosP*sinY, -sinP) → Yaw=atan2(Y,X), Pitch=-asin(Z).
	FRotator RotatorFromDirection(const FVector& Dir)
	{
		const FVector D = (Dir.Length() > 1e-6f) ? Dir.Normalized() : FVector::ForwardVector;
		FRotator Result;
		Result.Yaw = std::atan2(D.Y, D.X) * RadToDeg;
		Result.Pitch = -std::asin(std::clamp(D.Z, -1.0f, 1.0f)) * RadToDeg;
		Result.Roll = 0.0f;
		return Result;
	}
}

UBeamAttackComponent::UBeamAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UBeamAttackComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
}

bool UBeamAttackComponent::ComputeAim(FVector& OutOrigin, FVector& OutDirection) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	FVector Direction = FVector::ForwardVector;
	bool bResolved = false;

	// Lua 가 지정한 명시 방향이 있으면 카메라/액터 조준보다 우선. 궁극기 연출에서 카메라가
	// 캐릭터(얼굴)를 비추는 동안에도 빔이 보스로 진행하도록 한다.
	if (bHasAimOverride)
	{
		Direction = AimOverrideDirection;
		bResolved = true;
	}

	if (!bResolved && bUseCameraAim)
	{
		if (UWorld* World = GetWorld())
		{
			FMinimalViewInfo POV;
			if (World->GetActivePOV(POV))
			{
				Direction = POV.Rotation.GetForwardVector();
				bResolved = true;
			}
		}
	}

	if (!bResolved)
	{
		// 카메라 미사용 또는 POV 없음 → 캐릭터 forward.
		Direction = Owner->GetActorRotation().GetForwardVector();
	}

	Direction = (Direction.Length() > 1e-6f) ? Direction.Normalized() : FVector::ForwardVector;

	const FVector OwnerLocation = Owner->GetActorLocation();
	OutDirection = Direction;
	// actor 위치에서 camera 방향으로 SpawnForwardOffset(=1 transform) 만큼 앞. 회전 정렬은 하지 않는다.
	OutOrigin = OwnerLocation + Direction * SpawnForwardOffset;
	return true;
}

bool UBeamAttackComponent::IsBossActor(const AActor* Candidate) const
{
	if (!Candidate)
	{
		return false;
	}
	if (Candidate->HasTag(FName(BossTagName)))
	{
		return true;
	}
	const FString Name = Candidate->GetName();
	return Name.find("Boss") != FString::npos || Name.find("boss") != FString::npos;
}

UParticleSystemComponent* UBeamAttackComponent::SpawnBeamComponent(const FVector& Origin, const FVector& Direction)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	UParticleSystemComponent* Beam = Owner->AddComponent<UParticleSystemComponent>();
	if (!Beam)
	{
		return nullptr;
	}

	Beam->SetFName(FName("PlayerBeamEmitter"));
	if (USceneComponent* Root = Owner->GetRootComponent())
	{
		Beam->AttachToComponent(Root);
	}
	TemplateBeamDistance = 0.0f;
	TemplateBeamSpeed = 0.0f;
	if (UParticleSystem* Template = FParticleSystemManager::Get().Load(BeamTemplatePath))
	{
		Beam->SetTemplate(Template);
		// 충돌을 렌더와 동기화하기 위해 템플릿 빔의 Full 길이(Distance)와 끝점 연장 속도(Speed)를 읽어둔다.
		if (UParticleEmitter* Emitter = Template->GetEmitter(0))
		{
			if (UParticleLODLevel* LOD = Emitter->GetCurrentLODLevel(0))
			{
				if (UParticleModuleTypeDataBeam* BeamTD = Cast<UParticleModuleTypeDataBeam>(LOD->TypeDataModule))
				{
					TemplateBeamDistance = BeamTD->EvaluateDistance(0.0f);
					TemplateBeamSpeed = BeamTD->Speed;
				}
			}
		}
	}
	// camera 시선 방향 1 transform 앞에 배치하고, 빔이 그 방향으로 진행하도록 정렬한다.
	Beam->SetWorldLocation(Origin);
	Beam->SetWorldRotation(RotatorFromDirection(Direction));
	// 전체 크기 스케일은 상수(BeamScale). 길이 연장은 템플릿 Speed(렌더)와 충돌식(ApplyBeamDamageAlong)이 담당.
	Beam->SetRelativeScale(FVector(BeamScale, BeamScale, BeamScale));
	Beam->Activate(true);
	return Beam;
}

void UBeamAttackComponent::DestroyBeamComponent()
{
	if (UParticleSystemComponent* Beam = BeamComponent.Get())
	{
		Beam->Deactivate();
		if (AActor* Owner = GetOwner())
		{
			Owner->RemoveComponent(Beam);
		}
	}
	BeamComponent = nullptr;
}

void UBeamAttackComponent::FireBeam()
{
	// 재시전 시 기존 빔 정리.
	if (bBeamActive)
	{
		DestroyBeamComponent();
	}

	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	if (!ComputeAim(Origin, Direction))
	{
		return;
	}

	BeamOrigin = Origin;
	BeamDirection = Direction;
	BeamComponent = SpawnBeamComponent(Origin, Direction);
	bBeamActive = true;
	BeamAge = 0.0f;
	DamageAccumulator = 0.0f;
	bHasKilledBoss = false;   // 새 시전 — 이전 시전의 킬 플래그 초기화.

	UE_LOG("[BeamAttack] FireBeam owner=%s origin=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f) range=%.2f dur=%.2f",
		GetOwner() ? GetOwner()->GetName().c_str() : "nil",
		Origin.X, Origin.Y, Origin.Z,
		Direction.X, Direction.Y, Direction.Z,
		BeamRange, BeamDuration);
}

void UBeamAttackComponent::EndBeam()
{
	DestroyBeamComponent();
	bBeamActive = false;
	BeamAge = 0.0f;
	DamageAccumulator = 0.0f;
	// 명시 방향 override 는 한 번의 시전에만 적용 — 다음 시전이 stale 방향을 쓰지 않도록 해제.
	bHasAimOverride = false;
}

void UBeamAttackComponent::SetAimDirection(const FVector& Direction)
{
	if (Direction.Length() <= 1e-6f)
	{
		return;
	}
	AimOverrideDirection = Direction.Normalized();
	bHasAimOverride = true;
}

void UBeamAttackComponent::ClearAimDirection()
{
	bHasAimOverride = false;
}

void UBeamAttackComponent::SetBeamScale(float InScale)
{
	BeamScale = InScale > 0.0f ? InScale : 0.0f;
	// 스케일은 상수(매 프레임 적용 안 함) — 시전 중 변경 시 살아있는 빔에 즉시 반영한다.
	if (UParticleSystemComponent* Beam = BeamComponent.Get())
	{
		Beam->SetRelativeScale(FVector(BeamScale, BeamScale, BeamScale));
	}
}

void UBeamAttackComponent::SetBeamDuration(float InDuration)
{
	// UPROPERTY 범위(Min=0, Max=60)에 맞춰 clamp. BeamDuration 은 매 tick 비교에 쓰여 즉시 반영된다.
	BeamDuration = std::clamp(InDuration, 0.0f, 60.0f);
	UE_LOG("[BeamAttack] SetBeamDuration owner=%s duration=%.3f",
		GetOwner() ? GetOwner()->GetName().c_str() : "nil", BeamDuration);
}

void UBeamAttackComponent::ApplyBeamDamageAlong(const FVector& Origin, const FVector& Direction)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 충돌을 렌더 끝점과 동기화. 렌더(ParticleEmitterInstance)는 VisibleLength=Speed×EvalTime 으로 끝점을
	// 전진시키므로, 충돌 사거리도 같은 식(Speed×BeamAge, Full=Distance 에서 클램프)을 따른다. Speed=0(즉시 full)이면
	// 종전처럼 Distance 전체. BeamRange 는 템플릿에 빔 타입데이터가 없을 때만 폴백. 전체 스케일은 상수 BeamScale.
	const float FullLength = (TemplateBeamDistance > 0.0f) ? TemplateBeamDistance : BeamRange;
	const float GrownLength = (TemplateBeamSpeed > 0.0f)
		? (std::min)(TemplateBeamSpeed * BeamAge, FullLength)
		: FullLength;
	const float EffectiveRange = GrownLength * BeamScale;
	const float EffectiveRadius = BeamRadius * BeamScale;
	const FVector End = Origin + Direction * EffectiveRange;
	FHitResult Hit;
	const uint32 Mask =
		ObjectTypeBit(ECollisionChannel::WorldStatic)
		| ObjectTypeBit(ECollisionChannel::WorldDynamic)
		| ObjectTypeBit(ECollisionChannel::Pawn)
		| ObjectTypeBit(ECollisionChannel::Trigger);
	const bool bHit = World->PhysicsSweepByObjectTypes(
		Origin,
		End,
		FQuat::Identity,
		FCollisionShape::MakeSphere(EffectiveRadius),
		Hit,
		Mask,
		GetOwner());

	if (bDrawDebug)
	{
		DrawDebugLine(World, Origin, bHit ? Hit.WorldHitLocation : End,
			bHit && IsBossActor(Hit.HitActor) ? FColor::Green() : FColor::Red(), 0.05f);
	}

	if (!bHit)
	{
		return;
	}

	AActor* Target = Hit.HitActor;
	// 충돌 판정 결과에 따라 보스에게만 대미지 — 보스가 아니면 아무것도 하지 않는다.
	if (!Target || Target == GetOwner() || !IsBossActor(Target) || DamagePerTick <= 0.0f)
	{
		return;
	}

	if (UBulletHellDamageReceiverComponent* DamageReceiver = Target->GetComponentByClass<UBulletHellDamageReceiverComponent>())
	{
		const float Applied = DamageReceiver->ApplyDamageFromSource(DamagePerTick, EBossDamageSource::Beam);

		// 마무리 일격(보스 체력 1→0) 감지. 게이트상 보스 체력 0 은 Beam 으로만 도달하고, 이후 틱은
		// Applied=0(GetDamaged 가 0 반환)이라 이 분기는 마지막 일격 1프레임에만 참이 된다.
		if (Applied > 0.0f)
		{
			if (APawn* BossPawn = Cast<APawn>(Target); BossPawn && BossPawn->GetCurrentHealth() <= 0.0f)
			{
				bHasKilledBoss = true;   // Lua 가 폴링 → 빔 정지 + 카메라 조기 복원.

				// hit 지점에 explosion 1회 spawn (spawn-once, 정리 불필요).
				if (!KillExplosionPath.empty() && KillExplosionPath != "None")
				{
					FGameplayStatics::SpawnEmitterAtLocation(World, KillExplosionPath, Hit.WorldHitLocation);
					UE_LOG("[BeamAttack] kill explosion spawned at (%.2f,%.2f,%.2f)",
						Hit.WorldHitLocation.X, Hit.WorldHitLocation.Y, Hit.WorldHitLocation.Z);
				}
			}
		}
	}
}

void UBeamAttackComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	if (!bBeamActive)
	{
		return;
	}

	BeamAge += DeltaTime;
	DamageAccumulator += DeltaTime;

	// 일직선 고정(보스 추적 안 함): 시전 시작 시 캡처한 BeamOrigin/BeamDirection 으로 판정.
	if (DamageTickInterval > 0.0f)
	{
		while (DamageAccumulator >= DamageTickInterval)
		{
			DamageAccumulator -= DamageTickInterval;
			ApplyBeamDamageAlong(BeamOrigin, BeamDirection);
		}
	}
	else
	{
		ApplyBeamDamageAlong(BeamOrigin, BeamDirection);
	}

	if (BeamDuration > 0.0f && BeamAge >= BeamDuration)
	{
		EndBeam();
	}
}
