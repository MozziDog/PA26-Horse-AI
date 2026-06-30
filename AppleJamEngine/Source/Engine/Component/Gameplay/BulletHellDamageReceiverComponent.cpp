#include "BulletHellDamageReceiverComponent.h"

#include "GameFramework/Camera/PlayerCameraManager.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Pawn/BossCharacter.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "Runtime/Engine.h"

#include <algorithm>

UBulletHellDamageReceiverComponent::UBulletHellDamageReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bTickEnabled = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

float UBulletHellDamageReceiverComponent::ApplyDamage(float DamageAmount)
{
	// 출처 미지정 경로(Lua/리플렉션/보스 탄막→플레이어 등) — Generic 으로 위임.
	return ApplyDamageFromSource(DamageAmount, EBossDamageSource::Generic);
}

float UBulletHellDamageReceiverComponent::ApplyDamageFromSource(float DamageAmount, EBossDamageSource Source)
{
	if (!bDamageEnabled)
	{
		return 0.0f;
	}

	const float ClampedDamage = (std::max)(0.0f, DamageAmount);
	if (ClampedDamage <= 0.0f)
	{
		return 0.0f;
	}

	APawn* PawnOwner = GetOwnerPawn();
	if (!PawnOwner)
	{
		return 0.0f;
	}

	float DamageToApply = ClampedDamage;

	// 처형 게이트 — 소유자가 보스일 때만 적용. 체력 1(처형 임계) 미만으로는 Beam(궁극기)만 깎을 수
	// 있다. 일반 공격은 체력을 1까지만 깎고, 이미 1 이하면 완전히 막힌다(0 반환).
	if (Cast<ABossCharacter>(PawnOwner) && Source != EBossDamageSource::Beam)
	{
		constexpr float ExecuteFloor = 1.0f;
		const float MaxAllowed = (std::max)(0.0f, PawnOwner->GetCurrentHealth() - ExecuteFloor);
		DamageToApply = (std::min)(DamageToApply, MaxAllowed);
		if (DamageToApply <= 0.0f)
		{
			return 0.0f;
		}
	}

	const float AppliedDamage = PawnOwner->GetDamaged(DamageToApply);
	if (AppliedDamage > 0.0f)
	{
		TotalDamageForwarded += AppliedDamage;
		++HitCount;

		UWorld* World = GEngine ? GEngine->GetWorld() : nullptr;
		APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		if (PlayerController && PlayerController->GetPossessedPawn() == PawnOwner)
		{
			if (APlayerCameraManager* CameraManager = PlayerController->GetPlayerCameraManager())
			{
				CameraManager->StartDamageVignettePulse(0.5f);
			}
		}
	}
	return AppliedDamage;
}

APawn* UBulletHellDamageReceiverComponent::GetOwnerPawn() const
{
	return Cast<APawn>(GetOwner());
}
