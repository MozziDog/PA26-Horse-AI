#pragma once

#include "Component/ActorComponent.h"

#include "Source/Engine/Component/Gameplay/BulletHellDamageReceiverComponent.generated.h"

class APawn;

// 보스 데미지 출처 — 체력 1(처형 임계)에서 어떤 공격이 마지막 일격을 넣을 수 있는지 구분한다.
// Beam(궁극기)만 처형 가능(1→0). Arrow/Spray 등은 체력을 1까지만 깎고 그 이하로는 막힌다.
enum class EBossDamageSource : uint8
{
	Generic,
	Arrow,
	Spray,
	Beam,
};

UCLASS()
class UBulletHellDamageReceiverComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UBulletHellDamageReceiverComponent();
	~UBulletHellDamageReceiverComponent() override = default;

	UFUNCTION(Callable, Category="Bullet Hell|Damage Receiver")
	float ApplyDamage(float DamageAmount);

	// 출처를 함께 받는 버전. 소유자가 보스일 때만 처형 게이트 적용 — 체력 1 이하에서는 Beam 만
	// 통과(1→0)하고, 그 외 공격은 체력을 1 미만으로 깎지 못한다(0 반환). 보스가 아니면 게이트 없음.
	// (UFUNCTION 아님 — 리플렉션 시그니처를 건드리지 않아 generated 헤더 재생성이 불필요하다.)
	float ApplyDamageFromSource(float DamageAmount, EBossDamageSource Source);

	UFUNCTION(Callable, Category="Bullet Hell|Damage Receiver")
	void SetDamageEnabled(bool bEnabled) { bDamageEnabled = bEnabled; }

	UFUNCTION(Pure, Category="Bullet Hell|Damage Receiver")
	bool IsDamageEnabled() const { return bDamageEnabled; }

	UFUNCTION(Pure, Category="Bullet Hell|Damage Receiver")
	int32 GetHitCount() const { return HitCount; }

	UFUNCTION(Pure, Category="Bullet Hell|Damage Receiver")
	float GetTotalDamageForwarded() const { return TotalDamageForwarded; }

private:
	APawn* GetOwnerPawn() const;

	UPROPERTY(Save, Category="Bullet Hell|Damage Receiver", DisplayName="Total Damage Forwarded")
	float TotalDamageForwarded = 0.0f;

	UPROPERTY(Save, Category="Bullet Hell|Damage Receiver", DisplayName="Hit Count")
	int32 HitCount = 0;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Damage Receiver", DisplayName="Damage Enabled")
	bool bDamageEnabled = true;
};
