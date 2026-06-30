#pragma once

#include "Component/ActorComponent.h"
#include "Math/Vector.h"

#include "Source/Engine/Component/Gameplay/BossPatternComponentBase.generated.h"

class AActor;
class UBulletHellComponent;

UENUM()
enum class EBossPatternStep : int32
{
	None,
	Windup,
	Task1,
	Task2,
	Task3,
	Recovery,
	Finished
};

struct FBossPatternContext
{
	AActor* BossActor = nullptr;
	AActor* TargetActor = nullptr;
	UBulletHellComponent* BulletHell = nullptr;
	float DeltaTime = 0.0f;
	float DistanceToTarget = 0.0f;
	FVector BossLocation = FVector::ZeroVector;
	FVector TargetLocation = FVector::ZeroVector;
	FVector DirectionToTarget = FVector::ForwardVector;
	FVector BossForward = FVector::ForwardVector;
	FVector BossRight = FVector::RightVector;
	FVector BossUp = FVector::UpVector;
	int32 BossPhase = 0;
	float BossHealthRatio = 1.0f;
};

struct FBossPatternDebugState
{
	FString ActivePatternName = "None";
	FString LastSelectedPatternName = "None";
	FString LastRejectedReason = "None";
	EBossPatternStep ActiveStep = EBossPatternStep::None;
	float ActiveStepElapsed = 0.0f;
	float ActivePatternElapsed = 0.0f;
	FString ActivePatternDebugText;
	int32 CandidateCount = 0;
	int32 UsableCandidateCount = 0;
	int32 SelectionCount = 0;
	int32 FallbackCount = 0;
	int32 ActivePatternSelectionCount = 0;
	int32 BossPhase = 0;
	float BossHealthRatio = 1.0f;
};

UCLASS()
class UBossPatternComponentBase : public UActorComponent
{
public:
	GENERATED_BODY()
	UBossPatternComponentBase();
	~UBossPatternComponentBase() override = default;

	// To add a new boss pattern, copy one existing BossPattern_*.h/.cpp pair,
	// rename the class/file/default PatternName, register the files in the
	// project and filters, then implement only the step helpers it needs.

	virtual bool GetCanUse(const FBossPatternContext& Context, FString* OutRejectReason) const;
	virtual void StartPattern(const FBossPatternContext& Context);
	virtual void TickPattern(float DeltaTime, const FBossPatternContext& Context);
	virtual void CancelPattern(const FBossPatternContext& Context);
	virtual bool IsPatternFinished() const;

	float GetWeight() const { return Weight; }
	float GetEffectiveWeight(const FBossPatternContext& Context) const;
	float GetCooldownRemaining() const { return CooldownRemaining; }
	const FString& GetPatternName() const { return PatternName; }
	EBossPatternStep GetCurrentStep() const { return CurrentStep; }
	float GetStepElapsed() const { return StepElapsed; }
	float GetPatternElapsed() const { return PatternElapsed; }
	int32 GetSelectionCount() const { return SelectionCount; }
	bool IsEnabled() const { return bEnabled; }
	bool BlocksImmediateRepeat() const { return bBlockImmediateRepeat; }
	virtual FString GetRuntimeDebugText() const;

	void NotifySelected();
	void TickCooldown(float DeltaTime);

protected:
	virtual void OnPatternStart(const FBossPatternContext& Context);
	virtual void OnPatternEnd(const FBossPatternContext& Context);
	virtual void OnStepEnter(EBossPatternStep Step, const FBossPatternContext& Context);
	virtual void TickCurrentStep(float DeltaTime, const FBossPatternContext& Context);
	virtual bool ShouldAdvanceStep(const FBossPatternContext& Context) const;
	virtual EBossPatternStep GetNextStep(EBossPatternStep CurrentStep) const;
	void FinishPattern(const FBossPatternContext& Context);
	void EnterStep(EBossPatternStep NewStep, const FBossPatternContext& Context);
	void FaceTarget(const FBossPatternContext& Context) const;

protected:
	UPROPERTY(Edit, Save, Category="Boss Pattern|Common", DisplayName="Enabled")
	bool bEnabled = true;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Common", DisplayName="Pattern Name")
	FString PatternName = "BasePattern";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Selection", DisplayName="Weight", Min=0.0f, Max=100.0f, Speed=0.1f)
	float Weight = 1.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Selection", DisplayName="Phase Weight 0", Min=0.0f, Max=100.0f, Speed=0.1f)
	float PhaseWeight0 = 1.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Selection", DisplayName="Phase Weight 1", Min=0.0f, Max=100.0f, Speed=0.1f)
	float PhaseWeight1 = 1.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Selection", DisplayName="Phase Weight 2", Min=0.0f, Max=100.0f, Speed=0.1f)
	float PhaseWeight2 = 1.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Selection", DisplayName="Cooldown", Min=0.0f, Max=300.0f, Speed=0.1f)
	float Cooldown = 1.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Selection", DisplayName="Initial Cooldown", Min=0.0f, Max=300.0f, Speed=0.1f)
	float InitialCooldown = 0.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Condition", DisplayName="Min Target Distance", Min=0.0f, Max=100000.0f, Speed=1.0f)
	float MinTargetDistance = 0.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Condition", DisplayName="Max Target Distance", Min=0.0f, Max=100000.0f, Speed=1.0f)
	float MaxTargetDistance = 100000.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Condition", DisplayName="Allowed Phase Mask")
	int32 AllowedPhaseMask = 0x7fffffff;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Timing", DisplayName="Windup Duration", Min=0.0f, Max=30.0f, Speed=0.01f)
	float WindupDuration = 0.25f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Timing", DisplayName="Recovery Duration", Min=0.0f, Max=30.0f, Speed=0.01f)
	float RecoveryDuration = 0.25f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Common", DisplayName="Face Target During Pattern")
	bool bFaceTargetDuringPattern = true;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Selection", DisplayName="Block Immediate Repeat")
	bool bBlockImmediateRepeat = true;

	EBossPatternStep CurrentStep = EBossPatternStep::None;
	float StepElapsed = 0.0f;
	float PatternElapsed = 0.0f;
	float CooldownRemaining = 0.0f;
	int32 SelectionCount = 0;
	bool bRunning = false;
	bool bFinished = true;
};
