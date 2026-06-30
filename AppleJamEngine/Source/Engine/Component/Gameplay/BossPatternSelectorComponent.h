#pragma once

#include "Component/ActorComponent.h"
#include "Component/Gameplay/BossPatternComponentBase.h"
#include "Object/FName.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Component/Gameplay/BossPatternSelectorComponent.generated.h"

class AActor;
class UBulletHellComponent;

UCLASS()
class UBossPatternSelectorComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UBossPatternSelectorComponent();
	~UBossPatternSelectorComponent() override = default;

	void BeginPlay() override;
	const FBossPatternDebugState& GetBossPatternDebugState() const { return DebugState; }
	void RefreshPatternComponents();

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	FBossPatternContext BuildContext(float DeltaTime);
	AActor* ResolveTargetActor() const;
	UBulletHellComponent* ResolveBulletHellComponent() const;
	float ResolveBossHealthRatio() const;
	int32 ComputeBossPhase(float BossHealthRatio) const;
	void TickPatternCooldowns(float DeltaTime);
	void TickActivePattern(float DeltaTime, const FBossPatternContext& Context);
	void ConsumeDebugRequests(const FBossPatternContext& Context);
	void TrySelectNextPattern(const FBossPatternContext& Context);
	UBossPatternComponentBase* SelectWeightedPattern(const FBossPatternContext& Context);
	UBossPatternComponentBase* FindPatternByName(const FString& PatternName) const;
	bool TryForcePattern(const FBossPatternContext& Context);
	void CancelActivePattern(const FBossPatternContext& Context);
	bool IsBlockedByRecentPattern(const UBossPatternComponentBase* Pattern) const;
	void RecordRecentPattern(const UBossPatternComponentBase* Pattern);
	void EnterFallbackIdle();
	void TickFallbackIdle(float DeltaTime);
	void StartPattern(UBossPatternComponentBase* Pattern, const FBossPatternContext& Context);
	void BroadcastPatternCustomEvent(const UBossPatternComponentBase* Pattern) const;
	void LogSelectionEvent(const char* EventName, const UBossPatternComponentBase* Pattern, const char* Reason) const;
	void DrawPatternDebug(const FBossPatternContext& Context);
	void LogPatternDebug(float DeltaTime);
	void UpdateDebugStateFromActive();
	void RecordOverlayStats(const FBossPatternContext& Context) const;

private:
	UPROPERTY(Edit, Save, Category="Boss Pattern|Selector", DisplayName="Auto Start")
	bool bAutoStart = true;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Selector", DisplayName="Enable Pattern Selection")
	bool bEnablePatternSelection = true;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Log Pattern Selection")
	bool bLogPatternSelection = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Draw Pattern Debug")
	bool bDrawPatternDebug = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Fallback", DisplayName="Fallback Idle Duration", Min=0.0f, Max=30.0f, Speed=0.01f)
	float FallbackIdleDuration = 0.5f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Selection", DisplayName="Repeat Block Count", Min=0, Max=16, Speed=1)
	int32 RepeatBlockCount = 1;

	AActor* TargetActor = nullptr;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Target", DisplayName="Target Actor Name")
	FString TargetActorName;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Target", DisplayName="Target Tag")
	FName TargetTag = FName::None;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Target", DisplayName="Auto Resolve Player Target")
	bool bAutoResolvePlayerTarget = true;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Forced Pattern Name")
	FString ForcedPatternName;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Force Pattern Request")
	bool ForcePatternRequest = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Force Pattern Ignore Conditions")
	bool bForcePatternIgnoreConditions = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Cancel Pattern Request")
	bool CancelPatternRequest = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Cancel Goes To Fallback")
	bool bCancelGoesToFallback = true;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Debug Log Interval", Min=0.0f, Max=30.0f, Speed=0.1f)
	float DebugLogInterval = 0.5f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Phase", DisplayName="Phase 1 Health Ratio Threshold", Min=0.0f, Max=1.0f, Speed=0.01f)
	float Phase1HealthRatioThreshold = 0.66f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Phase", DisplayName="Phase 2 Health Ratio Threshold", Min=0.0f, Max=1.0f, Speed=0.01f)
	float Phase2HealthRatioThreshold = 0.33f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Use Debug Boss Health Ratio")
	bool bUseDebugBossHealthRatio = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Debug Boss Health Ratio", Min=0.0f, Max=1.0f, Speed=0.01f)
	float DebugBossHealthRatio = 1.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Use Debug Boss Phase")
	bool bUseDebugBossPhase = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Debug", DisplayName="Debug Boss Phase", Min=0, Max=30, Speed=1)
	int32 DebugBossPhase = 0;

	TArray<TWeakObjectPtr<UBossPatternComponentBase>> PatternComponents;
	TWeakObjectPtr<UBossPatternComponentBase> ActivePattern;
	TArray<FString> RecentPatternNames;
	FBossPatternDebugState DebugState;
	float FallbackIdleRemaining = 0.0f;
	float DebugLogRemaining = 0.0f;
	bool bSelectionStarted = false;
};
