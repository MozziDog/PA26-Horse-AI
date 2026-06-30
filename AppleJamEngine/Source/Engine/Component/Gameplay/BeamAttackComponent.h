#pragma once

#include "Component/ActorComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Vector.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Component/Gameplay/BeamAttackComponent.generated.h"

class AActor;
class UParticleSystemComponent;

// =============================================================================
// UBeamAttackComponent
//   PlayerSprayProjectileComponent 를 본뜬 "일직선 빔 공격" 컴포넌트.
//   - FireBeam(): 시전 시작점. anim notify(UAnimNotify_BeamFire) 또는 motion-end
//     감지(Lua World.StartPlayerBeamAttack) 양쪽에서 호출하는 공용 진입점.
//   - 시전 시작 시 조준 방향을 1회 고정 → 보스를 추적하지 않는 일직선 빔.
//   - 빔 비주얼은 런타임에 AddComponent 한 UParticleSystemComponent(Beam.uasset).
//   - 매 DamageTickInterval 마다 origin→origin+dir*range 구 스윕으로 충돌 판정,
//     맞은 액터가 보스면 UBulletHellDamageReceiverComponent::ApplyDamage.
//   - BeamDuration 경과 시 빔 컴포넌트를 RemoveComponent 로 제거(시전 종료).
// =============================================================================
UCLASS()
class UBeamAttackComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UBeamAttackComponent();
	~UBeamAttackComponent() override = default;

	void BeginPlay() override;

	// 빔 시전 시작 — notify / motion-end 트리거가 호출하는 공용 진입점.
	UFUNCTION(Callable, Category="Beam Attack")
	void FireBeam();

	// 빔 즉시 종료 + 컴포넌트 제거.
	UFUNCTION(Callable, Category="Beam Attack")
	void EndBeam();

	UFUNCTION(Pure, Category="Beam Attack")
	bool IsBeamActive() const { return bBeamActive; }

	// 이번 시전에서 빔이 보스를 처치(HP 1→0)했는가. Lua(World.HasPlayerBeamKilledBoss)가 폴링해
	// 빔 정지 + 카메라 조기 복원에 사용한다. FireBeam 에서 매 시전 false 로 리셋된다.
	bool HasKilledBoss() const { return bHasKilledBoss; }

	// 빔 크기 배율을 런타임에 설정. 시전 중이면 살아있는 빔에 즉시 반영하고,
	// 다음 시전에는 spawn 시 이 값으로 비주얼·충돌이 함께 커진다.
	UFUNCTION(Callable, Category="Beam Attack")
	void SetBeamScale(float InScale);

	UFUNCTION(Pure, Category="Beam Attack")
	float GetBeamScale() const { return BeamScale; }

	// 빔 수명(초)을 런타임에 설정. TickComponent 가 매 틱 BeamAge>=BeamDuration 비교에 쓰므로
	// 진행 중 빔·다음 빔 모두 반영. 카메라 연출 시간에 맞춰 Lua(World.SetBeamDuration)에서 호출한다.
	UFUNCTION(Callable, Category="Beam Attack")
	void SetBeamDuration(float InDuration);

	// 다음 시전의 발사 방향(월드)을 명시적으로 지정 — 카메라 조준(bUseCameraAim)·액터 forward 를 모두 무시한다.
	// 궁극기 연출에서 카메라가 캐릭터(얼굴)를 비추는 동안에도 빔은 보스 방향으로 진행시키기 위함. 0 벡터는 무시.
	UFUNCTION(Callable, Category="Beam Attack")
	void SetAimDirection(const FVector& Direction);

	// 명시적 발사 방향 override 를 해제하고 기본 조준(카메라/액터)으로 복귀. EndBeam 에서도 자동 해제된다.
	UFUNCTION(Callable, Category="Beam Attack")
	void ClearAimDirection();

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	bool ComputeAim(FVector& OutOrigin, FVector& OutDirection) const;
	bool IsBossActor(const AActor* Candidate) const;
	void ApplyBeamDamageAlong(const FVector& Origin, const FVector& Direction);
	UParticleSystemComponent* SpawnBeamComponent(const FVector& Origin, const FVector& Direction);
	void DestroyBeamComponent();

	UPROPERTY(Edit, Save, Category="Beam Attack", DisplayName="Beam Template", AssetType="UParticleSystem")
	FString BeamTemplatePath = "Content/Particle System/Beam.uasset";

	UPROPERTY(Edit, Save, Category="Beam Attack", DisplayName="Beam Range", Min=0.1f, Max=10000.0f, Speed=0.1f)
	float BeamRange = 20.0f;

	UPROPERTY(Edit, Save, Category="Beam Attack", DisplayName="Beam Radius", Min=0.001f, Max=100.0f, Speed=0.01f)
	float BeamRadius = 0.3f;

	// 빔 전체 크기 배율(asset 무수정, 컴포넌트 scale = 굵기·최대 길이). 시전 내내 상수로 유지된다.
	// "짧게 시작→연장"은 템플릿 Speed(렌더 끝점 전진)와 그에 동기화된 충돌이 담당한다(ApplyBeamDamageAlong).
	UPROPERTY(Edit, Save, Category="Beam Attack", DisplayName="Beam Scale", Min=0.01f, Max=100.0f, Speed=0.05f)
	float BeamScale = 1.0f;

	UPROPERTY(Edit, Save, Category="Beam Attack", DisplayName="Beam Duration", Min=0.0f, Max=60.0f, Speed=0.05f)
	float BeamDuration = 1.0f;

	UPROPERTY(Edit, Save, Category="Beam Attack", DisplayName="Damage Tick Interval", Min=0.01f, Max=5.0f, Speed=0.01f)
	float DamageTickInterval = 0.1f;

	UPROPERTY(Edit, Save, Category="Beam Attack", DisplayName="Damage Per Tick", Min=0.0f, Max=1000000.0f, Speed=0.1f)
	float DamagePerTick = 0.05f;

	// 마무리 일격(보스 체력 1→0)이 적중한 hit 지점에 1회 spawn 할 explosion 파티클 경로.
	// world-spawn 으로 렌더 검증된 BossBulletDestroyVfx 를 기본값으로 둔다(추후 전용 explosion 으로 교체 가능).
	UPROPERTY(Edit, Save, Category="Beam Attack", DisplayName="Boss Kill Explosion", AssetType="UParticleSystem")
	FString KillExplosionPath = "Content/Particle System/explosion.uasset";

	// actor camera 방향으로 origin 을 앞쪽으로 밀어내는 거리 (1 = 1 transform unit).
	UPROPERTY(Edit, Save, Category="Beam Attack", DisplayName="Forward Distance", Min=0.0f, Max=100.0f, Speed=0.1f)
	float SpawnForwardOffset = 1.0f;

	// true: 카메라(조준) forward 로 발사. false: 캐릭터 forward 로 발사.
	// (bHasAimOverride=true 이면 둘 다 무시하고 AimOverrideDirection 사용 — 궁극기 연출용.)
	UPROPERTY(Edit, Save, Category="Beam Attack", DisplayName="Use Camera Aim")
	bool bUseCameraAim = true;

	UPROPERTY(Edit, Save, Category="Beam Attack", DisplayName="Draw Debug")
	bool bDrawDebug = false;

	// Lua 가 명시적으로 지정한 발사 방향(월드, 정규화됨). true 면 ComputeAim 이 카메라 POV·액터 forward 대신 사용.
	// 궁극기 연출에서 카메라는 캐릭터를 비추되 빔은 보스로 보내기 위함. FireBeam 후 EndBeam 에서 해제된다.
	bool    bHasAimOverride = false;
	FVector AimOverrideDirection = FVector::ForwardVector;

	// 런타임 상태 — 시전 시작 시 캡처한 고정 일직선.
	TWeakObjectPtr<UParticleSystemComponent> BeamComponent;
	bool    bBeamActive = false;
	bool    bHasKilledBoss = false;   // 이번 시전에서 보스 처치(1→0) 여부 — FireBeam 에서 리셋.
	float   TemplateBeamDistance = 0.0f; // 스폰 시 읽은 템플릿 빔 Full 길이(로컬). 0=BeamRange 폴백.
	float   TemplateBeamSpeed = 0.0f;    // 스폰 시 읽은 템플릿 빔 연장 속도(로컬/초). >0 이면 충돌도 끝점 전진에 동기화.
	float   BeamAge = 0.0f;
	float   DamageAccumulator = 0.0f;
	FVector BeamOrigin = FVector::ZeroVector;
	FVector BeamDirection = FVector::ForwardVector;
};
