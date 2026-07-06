#include "BossPatternSelectorComponent.h"

#include "Component/Gameplay/BulletHellComponent.h"
#include "Component/Script/LuaBlueprintComponent.h"
#include "Core/Logging/Log.h"
#include "Core/TickFunction.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/GameMode/GameplayStatics.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "Component/Gameplay/BossPatternDebug.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>

namespace
{
	FVector SafeDirection(const FVector& Direction, const FVector& Fallback)
	{
		return Direction.IsNearlyZero() ? Fallback : Direction.Normalized();
	}

	float RandomUnitFloat()
	{
		return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
	}

	const char* LexToString(EBossPatternStep Step)
	{
		switch (Step)
		{
		case EBossPatternStep::Windup:
			return "Windup";
		case EBossPatternStep::Task1:
			return "Task1";
		case EBossPatternStep::Task2:
			return "Task2";
		case EBossPatternStep::Task3:
			return "Task3";
		case EBossPatternStep::Recovery:
			return "Recovery";
		case EBossPatternStep::Finished:
			return "Finished";
		default:
			return "None";
		}
	}
}

UBossPatternSelectorComponent::UBossPatternSelectorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.SetTickGroup(TG_PostPhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PostPhysics);
}

void UBossPatternSelectorComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	RefreshPatternComponents();
	bSelectionStarted = bAutoStart;
}

void UBossPatternSelectorComponent::RefreshPatternComponents()
{
	PatternComponents.clear();

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		if (UBossPatternComponentBase* Pattern = Cast<UBossPatternComponentBase>(Component))
		{
			PatternComponents.push_back(Pattern);
		}
	}
}

void UBossPatternSelectorComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	if (!bSelectionStarted || !bEnablePatternSelection)
	{
		FBossPatternContext Context = BuildContext(DeltaTime);
		DebugState.BossPhase = Context.BossPhase;
		DebugState.BossHealthRatio = Context.BossHealthRatio;
		UpdateDebugStateFromActive();
		RecordOverlayStats(Context);
		return;
	}

	FBossPatternContext Context = BuildContext(DeltaTime);
	DebugState.BossPhase = Context.BossPhase;
	DebugState.BossHealthRatio = Context.BossHealthRatio;
	ConsumeDebugRequests(Context);
	TickPatternCooldowns(DeltaTime);
	TickFallbackIdle(DeltaTime);
	TickActivePattern(DeltaTime, Context);

	if (!ActivePattern.Get() && FallbackIdleRemaining <= 0.0f)
	{
		TrySelectNextPattern(Context);
	}

	UpdateDebugStateFromActive();
	RecordOverlayStats(Context);
	DrawPatternDebug(Context);
	LogPatternDebug(DeltaTime);
}

FBossPatternContext UBossPatternSelectorComponent::BuildContext(float DeltaTime)
{
	FBossPatternContext Context;
	Context.BossActor = GetOwner();
	Context.TargetActor = ResolveTargetActor();
	Context.BulletHell = ResolveBulletHellComponent();
	Context.DeltaTime = DeltaTime;
	Context.BossHealthRatio = ResolveBossHealthRatio();
	Context.BossPhase = bUseDebugBossPhase ? (std::max)(0, DebugBossPhase) : ComputeBossPhase(Context.BossHealthRatio);

	if (Context.BossActor)
	{
		Context.BossLocation = Context.BossActor->GetActorLocation();
		Context.BossForward = SafeDirection(Context.BossActor->GetActorForward(), FVector::ForwardVector);
		Context.BossRight = SafeDirection(Context.BossActor->GetActorRight(), FVector::RightVector);
		Context.BossUp = SafeDirection(Context.BossActor->GetActorUp(), FVector::UpVector);
	}

	if (Context.TargetActor)
	{
		Context.TargetLocation = Context.TargetActor->GetActorLocation();
		Context.DistanceToTarget = FVector::Distance(Context.BossLocation, Context.TargetLocation);
		Context.DirectionToTarget = SafeDirection(Context.TargetLocation - Context.BossLocation, Context.BossForward);
	}
	else
	{
		Context.TargetLocation = Context.BossLocation + Context.BossForward;
		Context.DistanceToTarget = 0.0f;
		Context.DirectionToTarget = Context.BossForward;
	}

	return Context;
}

AActor* UBossPatternSelectorComponent::ResolveTargetActor() const
{
	if (AActor* ExplicitTarget = TargetActor)
	{
		return ExplicitTarget;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (!TargetActorName.empty())
	{
		for (AActor* Actor : World->GetActors())
		{
			if (Actor && Actor->GetName() == TargetActorName)
			{
				return Actor;
			}
		}
	}

	if (TargetTag.IsValid() && TargetTag != FName::None)
	{
		if (AActor* TaggedActor = FGameplayStatics::FindFirstActorByTag(World, TargetTag))
		{
			return TaggedActor;
		}
	}

	if (bAutoResolvePlayerTarget)
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			return PlayerController->GetPossessedPawn();
		}
	}

	return nullptr;
}

UBulletHellComponent* UBossPatternSelectorComponent::ResolveBulletHellComponent() const
{
	AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->GetComponentByClass<UBulletHellComponent>() : nullptr;
}

float UBossPatternSelectorComponent::ResolveBossHealthRatio() const
{
	if (bUseDebugBossHealthRatio)
	{
		return (std::max)(0.0f, (std::min)(1.0f, DebugBossHealthRatio));
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return 1.0f;
	}

	APawn* PawnOwner = Cast<APawn>(OwnerActor);
	if (!PawnOwner)
	{
		return 1.0f;
	}

	return PawnOwner->GetHealthRatio();
}

int32 UBossPatternSelectorComponent::ComputeBossPhase(float BossHealthRatio) const
{
	const float Phase1Threshold = (std::max)(0.0f, (std::min)(1.0f, Phase1HealthRatioThreshold));
	const float Phase2Threshold = (std::max)(0.0f, (std::min)(Phase1Threshold, Phase2HealthRatioThreshold));

	if (BossHealthRatio <= Phase2Threshold)
	{
		return 2;
	}

	if (BossHealthRatio <= Phase1Threshold)
	{
		return 1;
	}

	return 0;
}

void UBossPatternSelectorComponent::TickPatternCooldowns(float DeltaTime)
{
	for (TWeakObjectPtr<UBossPatternComponentBase>& PatternRef : PatternComponents)
	{
		if (UBossPatternComponentBase* Pattern = PatternRef.Get())
		{
			Pattern->TickCooldown(DeltaTime);
		}
	}
}

void UBossPatternSelectorComponent::TickActivePattern(float DeltaTime, const FBossPatternContext& Context)
{
	UBossPatternComponentBase* Pattern = ActivePattern.Get();
	if (!Pattern)
	{
		return;
	}

	Pattern->TickPattern(DeltaTime, Context);
	if (Pattern->IsPatternFinished())
	{
		LogSelectionEvent("finished", Pattern, nullptr);
		ActivePattern.Reset();
	}
}

void UBossPatternSelectorComponent::ConsumeDebugRequests(const FBossPatternContext& Context)
{
	if (CancelPatternRequest)
	{
		CancelPatternRequest = false;
		CancelActivePattern(Context);
	}

	if (ForcePatternRequest)
	{
		ForcePatternRequest = false;
		TryForcePattern(Context);
	}
}

void UBossPatternSelectorComponent::TrySelectNextPattern(const FBossPatternContext& Context)
{
	if (PatternComponents.empty())
	{
		DebugState.LastRejectedReason = "no pattern components";
		EnterFallbackIdle();
		return;
	}

	UBossPatternComponentBase* SelectedPattern = SelectWeightedPattern(Context);
	if (!SelectedPattern)
	{
		EnterFallbackIdle();
		return;
	}

	StartPattern(SelectedPattern, Context);
}

UBossPatternComponentBase* UBossPatternSelectorComponent::FindPatternByName(const FString& PatternName) const
{
	if (PatternName.empty())
	{
		return nullptr;
	}

	for (const TWeakObjectPtr<UBossPatternComponentBase>& PatternRef : PatternComponents)
	{
		UBossPatternComponentBase* Pattern = PatternRef.Get();
		if (Pattern && Pattern->GetPatternName() == PatternName)
		{
			return Pattern;
		}
	}

	return nullptr;
}

bool UBossPatternSelectorComponent::TryForcePattern(const FBossPatternContext& Context)
{
	UBossPatternComponentBase* Pattern = FindPatternByName(ForcedPatternName);
	if (!Pattern)
	{
		DebugState.LastRejectedReason = "forced pattern not found: " + ForcedPatternName;
		LogSelectionEvent("force failed", nullptr, DebugState.LastRejectedReason.c_str());
		return false;
	}

	if (!bForcePatternIgnoreConditions)
	{
		FString RejectReason;
		if (!Pattern->GetCanUse(Context, &RejectReason))
		{
			DebugState.LastRejectedReason = RejectReason.empty()
				? "forced pattern rejected"
				: RejectReason;
			LogSelectionEvent("force rejected", Pattern, DebugState.LastRejectedReason.c_str());
			return false;
		}
	}
	else if (!Pattern->IsEnabled())
	{
		DebugState.LastRejectedReason = Pattern->GetPatternName() + ": disabled";
		LogSelectionEvent("force rejected", Pattern, DebugState.LastRejectedReason.c_str());
		return false;
	}

	if (ActivePattern.Get())
	{
		CancelActivePattern(Context);
	}
	FallbackIdleRemaining = 0.0f;
	StartPattern(Pattern, Context);
	LogSelectionEvent("forced", Pattern, bForcePatternIgnoreConditions ? "ignore conditions" : "respect conditions");
	return true;
}

void UBossPatternSelectorComponent::CancelActivePattern(const FBossPatternContext& Context)
{
	UBossPatternComponentBase* Pattern = ActivePattern.Get();
	if (!Pattern)
	{
		DebugState.LastRejectedReason = "cancel requested with no active pattern";
		LogSelectionEvent("cancel ignored", nullptr, DebugState.LastRejectedReason.c_str());
		return;
	}

	Pattern->CancelPattern(Context);
	LogSelectionEvent("cancelled", Pattern, nullptr);
	ActivePattern.Reset();

	if (bCancelGoesToFallback)
	{
		EnterFallbackIdle();
	}
}

UBossPatternComponentBase* UBossPatternSelectorComponent::SelectWeightedPattern(const FBossPatternContext& Context)
{
	TArray<UBossPatternComponentBase*> UsablePatterns;
	float TotalWeight = 0.0f;
	FString LastRejectReason = "no usable pattern";
	int32 CandidateCount = 0;

	for (TWeakObjectPtr<UBossPatternComponentBase>& PatternRef : PatternComponents)
	{
		UBossPatternComponentBase* Pattern = PatternRef.Get();
		if (!Pattern)
		{
			continue;
		}

		++CandidateCount;
		FString RejectReason;
		if (!Pattern->GetCanUse(Context, &RejectReason))
		{
			if (!RejectReason.empty())
			{
				LastRejectReason = RejectReason;
			}
			continue;
		}

		const float EffectiveWeight = Pattern->GetEffectiveWeight(Context);
		if (EffectiveWeight <= 0.0f)
		{
			LastRejectReason = Pattern->GetPatternName() + ": phase weight zero";
			continue;
		}

		if (IsBlockedByRecentPattern(Pattern))
		{
			LastRejectReason = Pattern->GetPatternName() + ": repeat blocked";
			continue;
		}

		UsablePatterns.push_back(Pattern);
		TotalWeight += EffectiveWeight;
	}

	DebugState.CandidateCount = CandidateCount;
	DebugState.UsableCandidateCount = static_cast<int32>(UsablePatterns.size());

	if (UsablePatterns.empty() || TotalWeight <= 0.0f)
	{
		DebugState.LastRejectedReason = LastRejectReason;
		LogSelectionEvent("fallback", nullptr, LastRejectReason.c_str());
		return nullptr;
	}

	float Pick = RandomUnitFloat() * TotalWeight;
	for (UBossPatternComponentBase* Pattern : UsablePatterns)
	{
		Pick -= Pattern->GetEffectiveWeight(Context);
		if (Pick <= 0.0f)
		{
			return Pattern;
		}
	}

	return UsablePatterns.back();
}

bool UBossPatternSelectorComponent::IsBlockedByRecentPattern(const UBossPatternComponentBase* Pattern) const
{
	if (!Pattern || !Pattern->BlocksImmediateRepeat() || RepeatBlockCount <= 0)
	{
		return false;
	}

	const FString& PatternName = Pattern->GetPatternName();
	return std::find(RecentPatternNames.begin(), RecentPatternNames.end(), PatternName) != RecentPatternNames.end();
}

void UBossPatternSelectorComponent::RecordRecentPattern(const UBossPatternComponentBase* Pattern)
{
	if (!Pattern || RepeatBlockCount <= 0)
	{
		return;
	}

	RecentPatternNames.push_back(Pattern->GetPatternName());
	while (static_cast<int32>(RecentPatternNames.size()) > RepeatBlockCount)
	{
		RecentPatternNames.erase(RecentPatternNames.begin());
	}
}

void UBossPatternSelectorComponent::EnterFallbackIdle()
{
	ActivePattern.Reset();
	FallbackIdleRemaining = (std::max)(0.0f, FallbackIdleDuration);
	++DebugState.FallbackCount;
	DebugState.ActivePatternName = "FallbackIdle";
	DebugState.ActiveStep = EBossPatternStep::None;
	DebugState.ActiveStepElapsed = 0.0f;
	DebugState.ActivePatternElapsed = 0.0f;
}

void UBossPatternSelectorComponent::TickFallbackIdle(float DeltaTime)
{
	if (FallbackIdleRemaining <= 0.0f)
	{
		return;
	}

	FallbackIdleRemaining = (std::max)(0.0f, FallbackIdleRemaining - DeltaTime);
}

void UBossPatternSelectorComponent::StartPattern(UBossPatternComponentBase* Pattern, const FBossPatternContext& Context)
{
	if (!Pattern)
	{
		return;
	}

	Pattern->NotifySelected();
	Pattern->StartPattern(Context);
	ActivePattern = Pattern;
	RecordRecentPattern(Pattern);
	DebugState.LastSelectedPatternName = Pattern->GetPatternName();
	++DebugState.SelectionCount;
	BroadcastPatternCustomEvent(Pattern);
	LogSelectionEvent("selected", Pattern, nullptr);
}

void UBossPatternSelectorComponent::BroadcastPatternCustomEvent(const UBossPatternComponentBase* Pattern) const
{
	if (!Pattern || Pattern->GetPatternName().empty())
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		if (ULuaBlueprintComponent* LuaBlueprint = Cast<ULuaBlueprintComponent>(Component))
		{
			LuaBlueprint->CallCustomEvent(Pattern->GetPatternName());
		}
	}
}

void UBossPatternSelectorComponent::LogSelectionEvent(
	const char* EventName,
	const UBossPatternComponentBase* Pattern,
	const char* Reason) const
{
	if (!bLogPatternSelection)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	UE_LOG(
		"BossPattern %s. Owner=%s Pattern=%s Reason=%s Candidates=%d Usable=%d",
		EventName ? EventName : "event",
		OwnerActor ? OwnerActor->GetName().c_str() : "None",
		Pattern ? Pattern->GetPatternName().c_str() : "None",
		Reason ? Reason : "None",
		DebugState.CandidateCount,
		DebugState.UsableCandidateCount);
}

void UBossPatternSelectorComponent::DrawPatternDebug(const FBossPatternContext& Context)
{
	if (!bDrawPatternDebug || !Context.BossActor)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Center = Context.BossLocation + FVector::UpVector * 1.5f;
	const FColor Color = ActivePattern.Get() ? FColor::Green() : FColor(255, 180, 0);
	DrawDebugSphere(World, Center, 0.25f, 12, Color, 0.0f);
	DrawDebugLine(World, Center, Center + Context.DirectionToTarget * 1.5f, Color, 0.0f);
}

void UBossPatternSelectorComponent::LogPatternDebug(float DeltaTime)
{
	if (!bDrawPatternDebug && !bLogPatternSelection)
	{
		return;
	}

	if (DebugLogInterval <= 0.0f)
	{
		return;
	}

	DebugLogRemaining -= DeltaTime;
	if (DebugLogRemaining > 0.0f)
	{
		return;
	}

	DebugLogRemaining = DebugLogInterval;
	AActor* OwnerActor = GetOwner();
	UE_LOG(
		"BossPattern debug. Owner=%s Active=%s Step=%s StepElapsed=%.2f PatternElapsed=%.2f Candidates=%d Usable=%d Selections=%d Fallbacks=%d LastSelected=%s LastReject=%s ActiveSelected=%d Detail=%s",
		OwnerActor ? OwnerActor->GetName().c_str() : "None",
		DebugState.ActivePatternName.c_str(),
		LexToString(DebugState.ActiveStep),
		DebugState.ActiveStepElapsed,
		DebugState.ActivePatternElapsed,
		DebugState.CandidateCount,
		DebugState.UsableCandidateCount,
		DebugState.SelectionCount,
		DebugState.FallbackCount,
		DebugState.LastSelectedPatternName.c_str(),
		DebugState.LastRejectedReason.c_str(),
		DebugState.ActivePatternSelectionCount,
		DebugState.ActivePatternDebugText.c_str());
}

void UBossPatternSelectorComponent::UpdateDebugStateFromActive()
{
	if (UBossPatternComponentBase* Pattern = ActivePattern.Get())
	{
		DebugState.ActivePatternName = Pattern->GetPatternName();
		DebugState.ActiveStep = Pattern->GetCurrentStep();
		DebugState.ActiveStepElapsed = Pattern->GetStepElapsed();
		DebugState.ActivePatternElapsed = Pattern->GetPatternElapsed();
		DebugState.ActivePatternDebugText = Pattern->GetRuntimeDebugText();
		DebugState.ActivePatternSelectionCount = Pattern->GetSelectionCount();
		return;
	}

	if (FallbackIdleRemaining > 0.0f)
	{
		DebugState.ActivePatternName = "FallbackIdle";
		DebugState.ActiveStep = EBossPatternStep::None;
		DebugState.ActiveStepElapsed = FallbackIdleDuration - FallbackIdleRemaining;
		DebugState.ActivePatternElapsed = DebugState.ActiveStepElapsed;
		DebugState.ActivePatternDebugText = "";
		DebugState.ActivePatternSelectionCount = 0;
		return;
	}

	DebugState.ActivePatternName = "None";
	DebugState.ActiveStep = EBossPatternStep::None;
	DebugState.ActiveStepElapsed = 0.0f;
	DebugState.ActivePatternElapsed = 0.0f;
	DebugState.ActivePatternDebugText = "";
	DebugState.ActivePatternSelectionCount = 0;
}

void UBossPatternSelectorComponent::RecordOverlayStats(const FBossPatternContext& Context) const
{
#if STATS
	FBossPatternDebugSnapshot Snapshot;
	AActor* OwnerActor = GetOwner();
	Snapshot.OwnerName = OwnerActor ? OwnerActor->GetName() : "None";
	Snapshot.ActivePatternName = DebugState.ActivePatternName;
	Snapshot.LastSelectedPatternName = DebugState.LastSelectedPatternName;
	Snapshot.LastRejectedReason = DebugState.LastRejectedReason;
	Snapshot.ActiveDetail = DebugState.ActivePatternDebugText;
	Snapshot.CandidateCount = DebugState.CandidateCount;
	Snapshot.UsableCandidateCount = DebugState.UsableCandidateCount;
	Snapshot.SelectionCount = DebugState.SelectionCount;
	Snapshot.FallbackCount = DebugState.FallbackCount;
	Snapshot.bSelectionEnabled = bSelectionStarted && bEnablePatternSelection;
	Snapshot.BossPhase = Context.BossPhase;
	Snapshot.BossHealthRatio = Context.BossHealthRatio;
	Snapshot.Patterns.reserve(PatternComponents.size());

	for (const TWeakObjectPtr<UBossPatternComponentBase>& PatternRef : PatternComponents)
	{
		UBossPatternComponentBase* Pattern = PatternRef.Get();
		if (!Pattern)
		{
			continue;
		}

		FBossPatternDebugEntry Entry;
		Entry.PatternName = Pattern->GetPatternName();
		Entry.Detail = Pattern->GetRuntimeDebugText();
		Entry.CooldownRemaining = Pattern->GetCooldownRemaining();
		Entry.Weight = Pattern->GetEffectiveWeight(Context);
		Entry.SelectionCount = Pattern->GetSelectionCount();

		if (ActivePattern.Get() == Pattern)
		{
			Entry.Status = EBossPatternDebugStatus::Active;
			Entry.Reason = "active";
		}
		else if (Pattern->GetCooldownRemaining() > 0.0f)
		{
			char Buffer[64] = {};
			snprintf(Buffer, sizeof(Buffer), "cooldown %.2fs", Pattern->GetCooldownRemaining());
			Entry.Status = EBossPatternDebugStatus::Blocked;
			Entry.Reason = Buffer;
		}
		else
		{
			FString RejectReason;
			if (!Pattern->GetCanUse(Context, &RejectReason))
			{
				Entry.Status = EBossPatternDebugStatus::Blocked;
				Entry.Reason = RejectReason.empty() ? "blocked" : RejectReason;
			}
			else if (Pattern->GetEffectiveWeight(Context) <= 0.0f)
			{
				Entry.Status = EBossPatternDebugStatus::Blocked;
				Entry.Reason = "phase weight zero";
			}
			else if (IsBlockedByRecentPattern(Pattern))
			{
				Entry.Status = EBossPatternDebugStatus::Blocked;
				Entry.Reason = "repeat blocked";
			}
			else
			{
				Entry.Status = EBossPatternDebugStatus::Ready;
				Entry.Reason = "ready";
			}
		}

		Snapshot.Patterns.push_back(Entry);
	}

	BOSSPATTERN_DEBUG_ADD_COMPONENT(Snapshot);
#else
	(void)Context;
#endif
}
