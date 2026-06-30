#pragma once
// 풀링 가능한 발사체가 구현하는 순수 인터페이스 (UObject 아님 → UCLASS/GENERATED_BODY 없음).
// AActor 와 다중상속으로 붙인다: class AProjectileActor : public AActor, public IPoolableProjectile
//
// 주의: 이 엔진의 Cast<T>/IsA<T> 는 UClass 리플렉션 기반이라 '인터페이스' 캐스트를 못 한다.
//       그래서 풀은 RTTI 없이 동작하도록 템플릿(SpawnActor<T>)으로 컴파일타임 업캐스트하고,
//       GC/트랜스폼용 액터 핸들은 아래 AsActor() 로 되받는다.

struct FVector;
class  AActor;

class IPoolableProjectile
{
public:
	virtual ~IPoolableProjectile() = default;

	// GC 등록/트랜스폼에 쓸 액터 핸들. 구현은 보통 { return this; }.
	virtual AActor* AsActor() = 0;

	// 풀에 최초 편입될 때 단 1회: 무거운 컴포넌트 구성(InitDefaultComponents 등).
	virtual void OnPoolConstruct() = 0;

	// 풀에서 꺼내 재사용: 위치/속도 세팅 + 가시성·충돌·틱 ON.
	virtual void Activate(const FVector& Location, const FVector& Velocity) = 0;

	// 풀로 반환: 가시성·충돌·틱 OFF (절대 Destroy 하지 않음).
	virtual void Deactivate() = 0;

	// 잔여 상태 초기화: 속도/수명타이머/파티클/피격카운트 등.
	virtual void ResetState() = 0;
};