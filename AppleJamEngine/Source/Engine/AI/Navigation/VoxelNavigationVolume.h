#pragma once

#include "AI/Navigation/VoxelNavigationGrid.h"
#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/AI/Navigation/VoxelNavigationVolume.generated.h"

class UBoxComponent;

UCLASS()
class AVoxelNavigationVolume : public AActor
{
public:
	GENERATED_BODY()
	AVoxelNavigationVolume() = default;

	void InitDefaultComponents(const FVector& Extent = FVector(20.0f, 20.0f, 6.0f));
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void PostDuplicate() override;
	void OnPostLoad(FArchive& Ar) override;

	// Editor bake service only.  Runtime navigation must use LoadNavigationReference.
	bool BakeNavigationReference(const FString& OutputPath);
	bool LoadNavigationReference(const FString& InputPath);

	bool Contains(const FVector& Point) const;
	FVoxelNavigationPathResult FindPath(const FVector& Start, const FVector& Goal) const;
	const FVoxelNavigationGrid& GetGrid() const { return Grid; }

	UFUNCTION(Pure, Category="Navigation")
	bool IsNavigationBuilt() const { return Grid.IsBuilt(); }
	const FVoxelNavigationBuildSettings GetNavigationBuildSettings() const;

private:
	void RebindComponents();
	void DrawNavigationDebug() const;

	TWeakObjectPtr<UBoxComponent> VolumeBox = nullptr;
	FVoxelNavigationGrid Grid;
	UPROPERTY(Edit, Save, Category="Navigation|Baked Data", DisplayName="Reference JSON Path")
	FString ReferenceDataPath;

	UPROPERTY(Edit, Save, Category="Navigation|Agent", DisplayName="Agent Radius", Min=0.1f, Max=5.0f, Speed=0.05f)
	float AgentRadius = 0.6f;
	UPROPERTY(Edit, Save, Category="Navigation|Agent", DisplayName="Agent Height", Min=0.2f, Max=10.0f, Speed=0.05f)
	float AgentHeight = 2.0f;
	UPROPERTY(Edit, Save, Category="Navigation|Walkability", DisplayName="Max Walkable Slope", Min=0.0f, Max=89.0f, Speed=0.5f)
	float MaxWalkableSlopeDegrees = 30.0f;
	UPROPERTY(Edit, Save, Category="Navigation|Walkability", DisplayName="Max Neighbor Height Delta", Min=0.0f, Max=3.0f, Speed=0.05f)
	float MaxNeighborHeightDelta = 0.4f;
	UPROPERTY(Edit, Save, Category="Navigation|Path", DisplayName="Goal Acceptance Radius", Min=0.0f, Max=20.0f, Speed=0.1f)
	float GoalAcceptanceRadius = 2.0f;
	UPROPERTY(Edit, Save, Category="Navigation|Path", DisplayName="Max Start Snap Distance", Min=0.0f, Max=20.0f, Speed=0.1f)
	float MaxStartSnapDistance = 2.0f;
	UPROPERTY(Edit, Save, Category="Navigation|Path", DisplayName="Max Path Length (0 = Unlimited)", Min=0.0f, Max=10000.0f, Speed=1.0f)
	float MaxPathLength = 0.0f;

	UPROPERTY(Edit, Save, Category="Navigation|Debug", DisplayName="Draw Walkable Nodes")
	bool bDrawWalkableNodes = false;
	UPROPERTY(Edit, Save, Category="Navigation|Debug", DisplayName="Draw Chunk Boundaries")
	bool bDrawChunkBoundaries = false;
	UPROPERTY(Edit, Save, Category="Navigation|Debug", DisplayName="Max Debug Nodes", Min=0, Max=100000)
	int32 MaxDebugNodes = 4000;
	UPROPERTY(Edit, Save, Category="Navigation|Debug", DisplayName="Max Debug Chunks", Min=0, Max=100000)
	int32 MaxDebugChunks = 4000;

	// 디버그 정보 Detail 패널에 출력
	// NOTE: reflection이 size_t(=uint64) 프로퍼티를 지원하지 않아 detail 패널 표기값은 '잘린 값'일 수 있음
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Stats", DisplayName="Sampled Cells")
	int32 DebugSampledCells = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Stats", DisplayName="Walkable Nodes")
	int32 DebugWalkableNodes = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Stats", DisplayName="Directed Edges")
	int32 DebugDirectedEdges = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Stats", DisplayName="Raw Walkable Nodes")
	int32 DebugRawWalkableNodes = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Stats", DisplayName="Eroded Nodes")
	int32 DebugErodedNodes = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Stats", DisplayName="Built L1 Chunks")
	int32 DebugBuiltL1Chunks = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Stats", DisplayName="Abstract Nodes")
	int32 DebugAbstractNodes = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Stats", DisplayName="Abstract Edges")
	int32 DebugAbstractEdges = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Stats", DisplayName="Rejected Slope")
	int32 DebugRejectedSlope = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Stats", DisplayName="Rejected Clearance")
	int32 DebugRejectedClearance = 0;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Navigation|Stats", DisplayName="Build Time Ms")
	float DebugBuildTimeMs = 0.0f;
	UPROPERTY(Edit, ReadOnly, Transient, Category = "Navigation|Stats", DisplayName = "Peak Memory MB")
	float DebugPeakMemoryMB = 0.0f;
};
