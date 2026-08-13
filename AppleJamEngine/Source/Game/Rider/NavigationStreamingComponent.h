#pragma once

#include "Component/ActorComponent.h"
#include "AI/Navigation/NavigationStreamingService.h"

#include "Source/Game/Rider/NavigationStreamingComponent.generated.h"

// 액터 중심으로 일정 반경 내에 Navigation chunk 로드 상태를 유지하는 컴포넌트
// MaxLoadedChunks보다 많은 청크가 로드되었다면 거리 순으로 가장 먼 것부터 언로드
// NOTE: 현재 매 틱 체크를 수행하는데 성능 측정 후 필요하다면 1초에 한 번 같이 쿨타임 적용할 것
UCLASS()
class UNavigationStreamingComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UNavigationStreamingComponent();
	~UNavigationStreamingComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;

	UFUNCTION(Pure, Category="Navigation|Streaming")
	int32 GetLoadedChunkCount() const { return LoadedChunkCount; }

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	// 특정 NavigationVolume이 유지해야할 청크 좌표 리스트를 주면 로드할 건 로드하고 언로드할 건 언로드
	void UpdateStreamingForVolume(class AVoxelNavigationVolume& Volume, const TArray<FVoxelCoord>& DesiredCoords);
	float GetChunkDistanceSquared(const class AVoxelNavigationVolume& Volume, const FVoxelCoord& Coord, const FVector& ActorLocation) const;

	UPROPERTY(Edit, Save, Category="Navigation|Streaming", DisplayName="Load Radius", Min=1.0f, Max=10000.0f, Speed=1.0f)
	float LoadRadius = 35.0f;
	UPROPERTY(Edit, Save, Category="Navigation|Streaming", DisplayName="Max Loaded Chunks", Min=1, Max=100000, Speed=1.0f)
	int MaxLoadedChunks = 256;
	// Details 패널에 통계 표시용 ReadOnly property
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Streaming", DisplayName="Loaded Chunk Count")
	int LoadedChunkCount = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Streaming", DisplayName="Requested Chunk Count")
	int RequestedChunkCount = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Streaming", DisplayName="In Flight Chunk Count")
	int InFlightChunkCount = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Streaming", DisplayName="In Flight MB")
	float InFlightMemoryMB = 0.0f;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Streaming", DisplayName="Stale Load Discards")
	int StaleLoadDiscardCount = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Streaming", DisplayName="Last I/O Deserialize Ms")
	float LastIoDeserializeTimeMs = 0.0f;
	FNavigationStreamingService StreamingService;
};
