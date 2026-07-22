#pragma once

#include "Component/SceneComponent.h"
#include "Component/AI/BlackboardComponent.h"

#include "Source/Engine/Component/AI/CliffFanSensorComponent.generated.h"

class UHorseLocomotionComponent;

// 전방 부채꼴 각 slot 진행지점 아래로 raycast, 밟을 지면이 있는지 검사
// 방향은 HorseBBKeys::ObsFanAngles에 정의된 것 사용 (ObstacleFanSensor와 동일)
// 결과 기록은 HorseBBKeys::ObsGround를 키값으로 blackboard에 기록
// NOTE: 현재는 WorldStatic 대상만.
UCLASS()
class UCliffFanSensorComponent : public USceneComponent
{
public:
	GENERATED_BODY();
	UCliffFanSensorComponent() = default;

	void BeginPlay() override;

	// Editor time preview
	void ContributeSelectedVisuals(FScene& Scene) const override;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	// 현재 gait에 따른 탐지 거리
	float GetProbeDistance() const;

	// 보법 별 탐지 거리
	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Walk Probe Distance", Min=0.0f, Max=30.0f, Speed=0.1f)
	float WalkProbeDistance = 3.0f;
	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Trot Probe Distance", Min=0.0f, Max=30.0f, Speed=0.1f)
	float TrotProbeDistance = 4.0f;
	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Canter Probe Distance", Min=0.0f, Max=30.0f, Speed=0.1f)
	float CanterProbeDistance = 5.5f;
	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Gallop Probe Distance", Min=0.0f, Max=30.0f, Speed=0.1f)
	float GallopProbeDistance = 7.5f;

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Probe Up Distance", Min=0.0f, Max=5.0f, Speed=0.02f)
	float ProbeUpDist = 1.0f;		// m - 오르막길이나 점프로 오를 수 있는 단차 정도는 지면으로 판정

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Probe Down Distance", Min=0.0f, Max=10.0f, Speed=0.05f)
	float ProbeDownDist = 3.0f;     // m — 내리막길이나 뛰어내릴만한 높이도 지면으로 판정

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Draw Debug")
	bool  bDrawDebug = true;

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;
	TWeakObjectPtr<UHorseLocomotionComponent> LocomotionComp;
};
