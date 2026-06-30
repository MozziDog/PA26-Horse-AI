#include "pch.h"
#include "ProjectileActor.h"
#include "Engine/Runtime/Engine.h"

#include "Engine/Component/Primitive/StaticMeshComponent.h"
#include "Engine/Component/Particle/ParticleSystemComponent.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"
#include "Particle/Modules/ParticleModuleRequired.h"
#include "Particle/Modules/ParticleModuleSpawn.h"
#include "Particle/Modules/ParticleModuleLifetime.h"
#include "Particle/Modules/ParticleModuleLocation.h"
#include "Particle/Modules/ParticleModuleVelocity.h"
#include "Particle/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Particle/Distributions/DistributionFloatConstant.h"
#include "Particle/Distributions/DistributionVectorUniform.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/World.h"                   // GetProjectilePool() 완전한 타입
#include "GameFramework/ProjectilePoolSubSystem.h" // Release(this)

// ---------------------------------------------------------------------------
// Ribbon(궤적) emitter 코드 빌드 — 자산 직렬화/에디터 완성 전까지의 우회용.
// ParticleSystemActor 의 ConfigureMeshEmitter 패턴을 Ribbon 으로 옮긴 것.
// (모듈 파라미터를 코드에서 직접 박음 → 변경 시 재빌드 필요.)
// ---------------------------------------------------------------------------
namespace
{
	UDistributionFloatConstant* SetFloatConstant(UDistributionFloat*& Distribution, UObject* Outer, float Value)
	{
		if (Distribution)
		{
			UObjectManager::Get().DestroyObject(Distribution);
			Distribution = nullptr;
		}

		auto* NewDistribution = UObjectManager::Get().CreateObject<UDistributionFloatConstant>(Outer);
		if (NewDistribution)
		{
			NewDistribution->Constant = Value;
			Distribution = NewDistribution;
		}
		return NewDistribution;
	}

	UDistributionVectorUniform* SetVectorUniform(UDistributionVector*& Distribution, UObject* Outer, const FVector& Min, const FVector& Max)
	{
		if (Distribution)
		{
			UObjectManager::Get().DestroyObject(Distribution);
			Distribution = nullptr;
		}

		auto* NewDistribution = UObjectManager::Get().CreateObject<UDistributionVectorUniform>(Outer);
		if (NewDistribution)
		{
			NewDistribution->Min = Min;
			NewDistribution->Max = Max;
			Distribution = NewDistribution;
		}
		return NewDistribution;
	}

	// 시간 순으로 sample 된 입자를 이어 quad-strip 으로 렌더하는 Ribbon emitter 구성.
	// AddEmitter() 가 LOD0 + Required/Spawn + Lifetime/Location/Velocity/Color/Size 를
	// 이미 만들어 두므로, 여기서는 (a) Ribbon TypeData 부착 + (b) 파라미터만 덮어쓴다.
	void ConfigureRibbonEmitter(UParticleEmitter* Emitter)
	{
		if (!Emitter) return;
		Emitter->EmitterName = "Projectile Trail";

		UParticleLODLevel* LOD = Emitter->GetLODLevel(0);
		if (!LOD) return;

		// (1) TypeData = Ribbon — CreateInstance() 에서 FParticleRibbonEmitterInstance 생성
		if (!LOD->TypeDataModule)
		{
			auto* TypeData = UObjectManager::Get().CreateObject<UParticleModuleTypeDataRibbon>(LOD);
			if (TypeData)
			{
				TypeData->MaxTessellation = 8;
				TypeData->TangentTension  = 0.5f;
				TypeData->TilesPerTrail   = 1.0f;
				LOD->TypeDataModule = TypeData;
			}
		}

		// (2) Required: Ribbon/Beam 셰이더(BeamTrail.hlsl) 호환 머티리얼 + 월드 공간.
		//     bUseLocalSpace=false 라야 발사체가 날아간 자취가 월드에 남는다.
		if (UParticleModuleRequired* Required = LOD->RequiredModule)
		{
			Required->MaterialSlot   = "Content/Material/Particle/Ribbon1.uasset";
			Required->bUseLocalSpace = false;
		}

		// (3) Spawn rate — ribbon 의 sample 밀도(부드러움)를 좌우.
		if (UParticleModuleSpawn* Spawn = LOD->SpawnModule)
		{
			SetFloatConstant(Spawn->RateDistribution, Spawn, 60.0f);
		}

		// (4) AddEmitter() 가 만들어 둔 일반 모듈 파라미터만 갱신.
		for (UParticleModule* M : LOD->Modules)
		{
			if (auto* Lifetime = Cast<UParticleModuleLifetime>(M))
			{
				// Lifetime = 트레일이 유지되는 시간 ≒ 꼬리 길이.
				SetFloatConstant(Lifetime->LifetimeDistribution, Lifetime, 0.5f);
			}
			else if (auto* Loc = Cast<UParticleModuleLocation>(M))
			{
				// 점 소스(spread 0) — 궤적이 발사체 중심을 따라 한 줄로 이어지도록.
				SetVectorUniform(Loc->StartLocationDistribution, Loc, FVector::ZeroVector, FVector::ZeroVector);
			}
			else if (auto* Vel = Cast<UParticleModuleVelocity>(M))
			{
				// 트레일은 emitter 이동을 따라가므로 입자 자체 속도는 0.
				SetVectorUniform(Vel->StartVelocityDistribution, Vel, FVector::ZeroVector, FVector::ZeroVector);
			}
		}
	}
}

void AProjectileActor::BeginPlay()
{
	Super::BeginPlay();
}

// 	SpringArm->AttachToComponent(CapsuleComponent);
// root 가 capsule component
void AProjectileActor::InitDefaultComponents()
{
	StaticMeshComponent = AddComponent<UStaticMeshComponent>();
	ParticleSystemComponent = AddComponent<UParticleSystemComponent>();
	

	SetRootComponent(StaticMeshComponent);
	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	UStaticMesh* MeshAsset = FMeshManager::LoadStaticMesh("Content/Data/fireball/fireball.obj" , Device);
	
	StaticMeshComponent->SetStaticMesh(MeshAsset);
	StaticMeshComponent->SetRelativeScale(FVector(0.3f, 0.3f, 0.3f));
	ParticleSystemComponent->AttachToComponent(StaticMeshComponent);


	UParticleSystem* PS = UObjectManager::Get().CreateObject<UParticleSystem>();
	UParticleEmitter* RibbonEmitter = PS->AddEmitter();   // 코어/일반 모듈 자동 생성
	ConfigureRibbonEmitter(RibbonEmitter);                // Ribbon TypeData + 파라미터 세팅
	PS->BuildEmitters();                                  // payload layout 캐시 (안전망)

	ParticleSystemComponent->SetTemplate(PS);
	ParticleSystemComponent->Activate(false);

}

void AProjectileActor::PostDuplicate()
{
	Super::PostDuplicate();
	StaticMeshComponent = GetComponentByClass<UStaticMeshComponent>();
	ParticleSystemComponent = GetComponentByClass<UParticleSystemComponent>();
}
// ── IPoolableProjectile ─────────────────────────────────────────────────────

void AProjectileActor::OnPoolConstruct()
{
	if (bPoolConstructed) return;     // 풀 최초 편입 시 1회만
	bPoolConstructed = true;
	InitDefaultComponents();          // 무거운 컴포넌트 구성 + 렌더 상태 생성

	// [중요] 현재 Prewarm 은 UWorld::BeginPlay() 안(bHasBegunPlay=true 이후)에서 호출된다.
	// 그러면 CreatePooled 의 SpawnActor 시점에 AActor::BeginPlay 가 '컴포넌트 생성(InitDefaultComponents)
	// 이전'에 이미 실행돼 끝난다(가드됨). 결과적으로 방금 만든 메시/파티클 컴포넌트는 BeginPlay 를 못 받아
	// bComponentHasBegunPlay=false 가 되고, Activate 의 SetSimulatePhysics/SetCollisionEnabled/
	// SetLinearVelocity 가 전부 no-op → 물리 바디가 생성되지 않아 발사체가 움직이지 않는다.
	// → 액터가 이미 begun play 라면, 방금 만든 컴포넌트에 BeginPlay 를 직접 보장한다.
	//   (컴포넌트는 막 생성돼 begun play 가 아님이 확실하므로 1회 호출, 중복 위험 없음)
	UE_LOG("[ProjConstruct] HasActorBegunPlay=%d", (int)HasActorBegunPlay());
	if (HasActorBegunPlay())
	{
		if (UStaticMeshComponent* Mesh = StaticMeshComponent.Get())
		{
			Mesh->BeginPlay();
		}
		if (UParticleSystemComponent* PSC = ParticleSystemComponent.Get())
		{
			PSC->BeginPlay();
		}
	}
}

void AProjectileActor::Activate(const FVector& Location, const FVector& Velocity)
{
	SetActorLocation(Location);
	SetVisible(true);                 // 렌더 가시성 ON (액터→컴포넌트 전파)

	CachedVelocity = Velocity;
	LifeTimeRemaining = DefaultLifeTime;

	// 물리 기반 등속 직진: dynamic 바디 + 중력 OFF + 선속도 1회 세팅 (damping 0 → 등속 유지).
	// 순서 중요 — (1) 플래그 설정 → (2) SetCollisionEnabled 가 바디 등록(플래그 읽음) → (3) 속도 적용.
	if (UStaticMeshComponent* Mesh = StaticMeshComponent.Get())
	{
		Mesh->SetSimulatePhysics(true);                                 // dynamic 바디
		Mesh->SetEnableGravity(false);                                  // 등속 → 중력 제거
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);  // 여기서 바디 등록(위 플래그 반영)
		Mesh->SetLinearVelocity(Velocity);                             // 등속(예: 0.5 m/s) 1회 세팅
		Mesh->SetAngularVelocity(FVector::ZeroVector);                 // 회전 없음

		// ── [진단] readVel 이 ~inVel 이면 바디·속도 OK(문제는 writeback/frozen),
		//    (0,0,0) 이면 바디 미등록(bComponentHasBegunPlay false 또는 shape 없음). ──
		const FVector ReadV = Mesh->GetLinearVelocity();
		UE_LOG("[ProjActivate] sim=%d coll=%d inVel=(%.3f,%.3f,%.3f) readVel=(%.3f,%.3f,%.3f)",
			(int)Mesh->GetSimulatePhysics(), (int)Mesh->IsCollisionEnabled(),
			Velocity.X, Velocity.Y, Velocity.Z, ReadV.X, ReadV.Y, ReadV.Z);
	}
	else
	{
		UE_LOG("[ProjActivate] StaticMeshComponent == NULL");
	}
	if (UParticleSystemComponent* PSC = ParticleSystemComponent.Get())
	{
		PSC->Activate(true);          // 파티클 리셋 후 재생
	}
	bNeedsTick = true;                // 수명 카운트다운용(이동은 물리가 담당)
}

void AProjectileActor::Deactivate()
{
	bNeedsTick = false;

	if (UStaticMeshComponent* Mesh = StaticMeshComponent.Get())
	{
		Mesh->SetLinearVelocity(FVector::ZeroVector);               // 정지 (바디 살아있을 때)
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);  // 물리 바디 등록 해제
		Mesh->SetSimulatePhysics(false);                            // dynamic 해제
	}
	if (UParticleSystemComponent* PSC = ParticleSystemComponent.Get())
	{
		PSC->Deactivate();
	}
	SetVisible(false);                // 렌더 가시성 OFF — 단, Destroy 는 하지 않음
}

void AProjectileActor::ResetState()
{
	CachedVelocity = FVector::ZeroVector;
	LifeTimeRemaining = 0.0f;
	// TODO(game): 소유자/관통 카운트/궤적/타이머 등 게임 측 잔여 상태 초기화
}

void AProjectileActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bNeedsTick) return;                      // 비활성(풀 보관) 상태 보호

	// ── [수정] 물리 바디는 비동기로 '다음 프레임'에 생성된다. Activate 의 1회 SetLinearVelocity 는
	//    바디 생성 전 타이밍이면 드롭되고, GetLinearVelocity 는 스냅샷 기반이라 그 뒤로도 0 → 영구 정지.
	//    → 매 프레임 목표 속도를 재적용해 바디가 살아난 직후부터 등속이 실리게 한다(중력 OFF·damping 0 → 등속). ──
	if (UStaticMeshComponent* Mesh = StaticMeshComponent.Get())
	{
		Mesh->SetLinearVelocity(CachedVelocity);   // 등속 재적용 (초기 드롭/슬립 보정)

		const FVector L = GetActorLocation();
		const FVector V = Mesh->GetLinearVelocity();
		UE_LOG("[ProjTick] life=%.2f loc=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f)",
			LifeTimeRemaining, L.X, L.Y, L.Z, V.X, V.Y, V.Z);
	}

	// 이동은 물리(dynamic + 중력 OFF + 등속)가 담당 → Tick 은 수명만 관리.
	LifeTimeRemaining -= DeltaTime;
	if (LifeTimeRemaining <= 0.0f)
	{
		if (UWorld* W = GetWorld())
		{
			if (FProjectilePoolSubsystem* Pool = W->GetProjectilePool())
			{
				Pool->Release(this);              // Deactivate + ResetState + 풀 보관
				return;
			}
		}
		Deactivate();                             // 안전망: 풀이 없으면 최소 비활성화
	}
}