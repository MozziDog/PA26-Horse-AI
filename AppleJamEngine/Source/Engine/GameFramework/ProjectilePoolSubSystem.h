#pragma once
#include <type_traits>
#include "Object/GarbageCollection.h"          // FGCObject, FReferenceCollector
#include "GameFramework/World.h"               // UWorld 완전한 타입 — 템플릿 World->SpawnActor<T>() 파싱에 필수 (순환 아님: World.h 는 이 헤더를 include 하지 않음)
#include "GameFramework/IPoolableProjectile.h"

// TMap/TArray/int32/FVector/IsValid 는 pch(CoreTypes.h, Vector.h)에서 주입됨 — 본 엔진 헤더 관례.
class UClass;

// ──────────────────────────────────────────────────────────────────────────
// 월드 종속 발사체 풀 서브시스템.
//  - UWorld 가 std::unique_ptr 로 소유 (PhysicsScene 패턴과 동일).
//  - FGCObject 상속 → 생성자에서 자동으로 GC 외부 루트 등록(AddExternalRoot),
//    소멸자에서 자동 해제(RemoveExternalRoot). 즉 보관/대여 중인 액터는 GC가 회수하지 않음.
//    (언리얼 UPROPERTY() 컨테이너가 GC를 막는 것과 동일 역할 — 단 여기선 수동 노출)
// ──────────────────────────────────────────────────────────────────────────
class FProjectilePoolSubsystem : public FGCObject
{
public:
	FProjectilePoolSubsystem() = default;       // FGCObject 기본생성자가 외부 루트 등록을 수행
	~FProjectilePoolSubsystem() override = default;

	// ── 월드 수명 훅 ──────────────────────────────────────────────
	void Initialize(UWorld* InWorld);           // UWorld::InitWorld() 끝에서 호출
	void Shutdown();                            // UWorld::EndPlay()/RouteWorldDestroyed() 에서 호출 (여기서만 실제 Destroy)

	// ── 풀 API (템플릿 = RTTI 불필요, T 는 AActor & IPoolableProjectile 둘 다 상속) ──
	template<typename T> void Prewarm(int32 Count);                         // 무거운 초기화 선지불
	template<typename T> T* Acquire(const FVector& Location, const FVector& Velocity);
	void Release(AActor* Projectile);           // 비활성화 + 리셋 후 보관 (파괴 X)

	// ── FGCObject: 풀이 들고 있는 모든 액터를 '도달 가능'으로 노출 ──
	void        AddReferencedObjects(FReferenceCollector& Collector) override;
	const char* GetReferencerName() const override { return "FProjectilePoolSubsystem"; }

private:
	template<typename T> IPoolableProjectile* CreatePooled();               // Spawn + OnPoolConstruct + Deactivate

private:
	UWorld* World = nullptr;                     // 월드가 소유 → 역참조(루팅 대상 아님)

	// 타입별 비활성(반환된) 풀. 요청하신 unordered_map<TypeId, vector<Actor*>> 와 동형.
	// 원소를 IPoolableProjectile* 로 보관 → Acquire/Release 마다 재캐스트 불필요(RTTI 회피),
	// GC 노출 시 AsActor() 로 UObject* 를 되받는다.
	TMap<UClass*, TArray<IPoolableProjectile*>> InactivePool;

	// 현재 대여 중(활성) 목록. Release(AActor*) 역참조 + 이중반환 검출 + 비행 중 GC 방지 루팅.
	TMap<AActor*, IPoolableProjectile*> ActiveLookup;
};

// ── 템플릿 구현 (헤더 인라인) ─────────────────────────────────────────────
template<typename T>
IPoolableProjectile* FProjectilePoolSubsystem::CreatePooled()
{
	static_assert(std::is_base_of_v<AActor, T>, "T must derive from AActor");
	static_assert(std::is_base_of_v<IPoolableProjectile, T>, "T must derive from IPoolableProjectile");

	// 무거운 초기화는 여기서 단 1회: 액터 생성 + 레벨/옥트리 등록 (+ bHasBegunPlay 면 BeginPlay).
	T* Actor = World->SpawnActor<T>();
	IPoolableProjectile* Pooled = static_cast<IPoolableProjectile*>(Actor); // 컴파일타임 업캐스트 (RTTI 없음)

	Pooled->OnPoolConstruct();   // InitDefaultComponents — 컴포넌트 + 렌더 상태 생성
	Pooled->Deactivate();        // 만들자마자 재워서 풀에 적재
	Pooled->ResetState();
	return Pooled;
}

template<typename T>
void FProjectilePoolSubsystem::Prewarm(int32 Count)
{
	TArray<IPoolableProjectile*>& Bucket = InactivePool[T::StaticClass()];
	Bucket.reserve(Bucket.size() + Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Bucket.push_back(CreatePooled<T>());
	}
}

template<typename T>
T* FProjectilePoolSubsystem::Acquire(const FVector& Location, const FVector& Velocity)
{
	TArray<IPoolableProjectile*>& Bucket = InactivePool[T::StaticClass()];

	IPoolableProjectile* Pooled = nullptr;
	if (!Bucket.empty())
	{
		Pooled = Bucket.back();      // 재사용: Spawn 비용 0
		Bucket.pop_back();
	}
	else
	{
		// [열린 질문] 풀 고갈 정책. 기본값 = on-demand 생성.
		//   대안 ① nullptr 반환(상한 고정) ② 가장 오래된 활성 객체 steal.
		Pooled = CreatePooled<T>();
	}

	AActor* Actor = Pooled->AsActor();
	ActiveLookup[Actor] = Pooled;
	Pooled->Activate(Location, Velocity);
	return static_cast<T*>(Actor);   // 버킷은 T 인스턴스만 보관하므로 안전
}